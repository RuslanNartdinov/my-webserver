#include "webserv/net/Connection.hpp"
#include "webserv/Log.hpp"

#include <sys/socket.h>
#include <poll.h>
#include <errno.h>
#include <sstream>
#include <map>
#include <vector>
#include <unistd.h>
#include <ctime>

#include "webserv/http/Router.hpp"
#include "webserv/config/Config.hpp"
#include "webserv/http/StaticHandler.hpp"
#include "webserv/http/Cgi.hpp"
#include "webserv/utils/Time.hpp"
#include "webserv/utils/IO.hpp"
#include "webserv/net/ResponseBuilder.hpp"
#include "webserv/net/UploadHandler.hpp"
#include "webserv/net/DeleteHandler.hpp"
#include "webserv/net/MethodGate.hpp"

namespace ws
{
    static std::string itoa10(int x) { std::ostringstream oss; oss << x; return oss.str(); }

    static const ServerConfig* pickDefaultServer(const Router* router,
                                                 const std::string& lhost, int lport)
    {
        if (!router) return 0;
        RouteMatch tmp = router->resolve(lhost, lport, "", "/");
        return tmp.server;
    }

    static std::string hexLower(size_t x) { std::ostringstream oss; oss << std::hex << std::nouppercase << x; return oss.str(); }

    static std::string genUploadName()
    {
        std::ostringstream oss;
        oss << "up_" << (unsigned long long)time(0)
            << "_"  << (unsigned long long)getpid()
            << "_"  << (unsigned long long)rand();
        return oss.str();
    }

    bool Connection::shouldKeepAlive(const HttpRequest& r) const
    {
        std::string ver = r.version;
        std::string c = r.getHeader("connection");
        for (size_t i = 0; i < c.size(); ++i)
            if (c[i] >= 'A' && c[i] <= 'Z') c[i] = char(c[i] - 'A' + 'a');
        bool ka = (ver == "HTTP/1.1") ? (c != "close") : (c == "keep-alive");
        if (_reqsOnConn >= MAX_KEEPALIVE) ka = false;
        return ka;
    }

    Connection::~Connection() { if (_fd >= 0) ::close(_fd); }

    void Connection::closeNow()
    {
        if (_fd >= 0) { ::close(_fd); _fd = -1; }
        _state = CLOSED;
    }

    short Connection::wantEvents() const { return _state == READ ? POLLIN : (_state == WRITE ? POLLOUT : 0); }

    void Connection::makeResponse(int code, const std::string& reason,
                                  const std::string& ctype,
                                  const std::string& body,
                                  const std::string& location)
    {
        const bool isHead = (_req.method == "HEAD");
        const size_t len = isHead ? 0 : body.size();
        makeResponseHeaders(code, reason, ctype, len, location, "");
        if (!isHead) _out += body;
    }

    void Connection::makeResponseHeaders(int code, const std::string& reason,
                                         const std::string& ctype, size_t clen,
                                         const std::string& location,
                                         const std::string& extra)
    {
        _out.clear();
        std::ostringstream oss;
        oss << "HTTP/1.1 " << code << ' ' << reason << "\r\n"
            << "Server: webserv-dev\r\n"
            << "Date: " << ws::httpDateNow() << "\r\n"
            << "Content-Type: " << ctype << "\r\n"
            << "Content-Length: " << (int)clen << "\r\n"
            << "Connection: " << (_curKeepAlive ? "keep-alive" : "close") << "\r\n";
        if (_curKeepAlive) oss << "Keep-Alive: timeout=5, max=100\r\n";
        if (!location.empty()) oss << "Location: " << location << "\r\n";
        if (!extra.empty())    oss << extra;
        oss << "\r\n";
        _out = oss.str();
        _state = WRITE;
    }

    void Connection::makeErrorWithPages(int code, const ServerConfig* srv)
    {
        std::string reason = (code == 400 ? "Bad Request" : code == 411 ? "Length Required"
                                   : code == 413 ? "Payload Too Large"
                                   : code == 501 ? "Not Implemented" : "Error");
        std::string body;
        std::string ctype = "text/plain; charset=utf-8";

        if (srv)
        {
            std::map<int, std::string>::const_iterator it = srv->error_pages.find(code);
            if (it != srv->error_pages.end())
            {
                std::string fs = it->second;
                if (!srv->root.empty() && (fs.size() < 2 || fs.substr(0, 2) != "./"))
                    fs = srv->root + "/" + fs;
                if (ws::readWholeFile(fs, body))
                {
                    ctype = "text/html; charset=utf-8";
                    makeResponse(code, reason, ctype, body);
                    return;
                }
            }
        }
        body = itoa10(code) + " " + reason + "\n";
        makeResponse(code, reason, ctype, body);
    }

    void Connection::makeChunkedResponse(int code, const std::string& reason,
                                         const std::string& ctype,
                                         const std::string& body,
                                         const std::string& extra)
    {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << code << ' ' << reason << "\r\n"
            << "Server: webserv-dev\r\n"
            << "Content-Type: " << ctype << "\r\n"
            << "Transfer-Encoding: chunked\r\n"
            << "Connection: " << (_curKeepAlive ? "keep-alive" : "close") << "\r\n";
        if (_curKeepAlive) oss << "Keep-Alive: timeout=5, max=100\r\n";
        if (!extra.empty())  oss << extra;
        oss << "\r\n";
        oss << hexLower(body.size()) << "\r\n";
        oss << body << "\r\n";
        oss << "0\r\n\r\n";
        _out = oss.str();
        _state = WRITE;
    }

    bool Connection::handlePostUpload(const RouteMatch& m)
{
    if (_req.method != "POST" || !m.location || !m.location->upload_enable || m.location->upload_store.empty())
        return false;

    size_t limit = 10 * 1024 * 1024;
    if (m.location->client_max_body_size)                   limit = m.location->client_max_body_size;
    else if (m.server && m.server->client_max_body_size)    limit = m.server->client_max_body_size;

    if (_req.body.size() > limit) {
        makeErrorWithPages(413, m.server);
        return true;
    }

    std::string serverRoot = (m.server && !m.server->root.empty()) ? m.server->root : std::string(".");
    std::string updir = m.location->upload_store;
    if (!updir.empty() && updir[0] != '/')
        updir = (serverRoot.back() == '/' ? serverRoot + updir : serverRoot + "/" + updir);

    if (!ws::ensureDirRecursive(updir)) {
        makeResponse(500, "Internal Server Error", "text/plain; charset=utf-8", "500 Internal Server Error\n");
        return true;
    }

    const std::string fileName = genUploadName();           // если нужно, замени на ws::genUploadName()
    const std::string outPath  = updir + "/" + fileName;

    if (!ws::writeBinary(outPath, _req.body)) {
        makeResponse(500, "Internal Server Error", "text/plain; charset=utf-8", "500 Internal Server Error\n");
        return true;
    }

    const std::string locationHdr = "/uploads/" + fileName;

    makeResponseHeaders(201, "Created", "text/plain; charset=utf-8", 12, locationHdr, "");
    if (_req.method != "HEAD")
        _out += "201 Created\n";
    _state = WRITE;
    return true;
}

    void Connection::onReadable()
    {
        if (_state != READ) return;

        char buf[8192];
        for (;;)
        {
            ssize_t n = ::recv(_fd, buf, sizeof(buf), 0);
            if (n > 0)
            {
                _parser.feed(buf, (size_t)n);

                for (;;)
                {
                    HttpRequest req;
                    HttpParser::Result r = _parser.parse(req);
                    if (r == HttpParser::NEED_MORE) break;

                    const ServerConfig* defSrv = pickDefaultServer(_router, _lhost, _lport);

                    if (r == HttpParser::OK)
                    {
                        _req = req;
                        _curKeepAlive = shouldKeepAlive(_req);

                        if (_req.version == "HTTP/1.1" && !_req.hasHeader("host"))
                        {
                            makeErrorWithPages(400, defSrv);
                            return;
                        }

                        RouteMatch m = _router->resolve(_lhost, _lport, _req.getHeader("host"), _req.target);

                        std::string checkMethod = (_req.method == "HEAD") ? "GET" : _req.method;

                        if (!ws::isImplemented(_req.method))
                        {
                            if (m.location && m.location->path == "/upload")
                            {
                                std::string allow = ws::buildAllowHeader(m.location);
                                makeResponseHeaders(405, "Method Not Allowed", "text/plain; charset=utf-8", 0, "", "Allow: " + allow + "\r\n");
                                return;
                            }
                            makeErrorWithPages(501, defSrv);
                            return;
                        }

                        if (!ws::isAllowed(m.location, checkMethod))
                        {
                            std::string allow = ws::buildAllowHeader(m.location);
                            makeResponseHeaders(405, "Method Not Allowed", "text/plain; charset=utf-8", 0, "", "Allow: " + allow + "\r\n");
                            return;
                        }

                        if (m.location && m.location->return_code >= 300 && m.location->return_code < 400 && !m.location->return_url.empty())
                        {
                            makeResponse(m.location->return_code, "Moved Permanently", "text/plain; charset=utf-8", "", m.location->return_url);
                            return;
                        }

                        if (_req.method == "POST")
                        {
                            if (handlePostUpload(m)) return;
                        }

                        {
                            CgiResult cgi;
                            if (m.location && m.server && CgiHandler::handle(*m.server, m.location, _req, cgi))
                            {
                                std::string ctype = "text/html; charset=utf-8";
                                std::map<std::string, std::string>::const_iterator ct = cgi.headers.find("content-type");
                                if (ct != cgi.headers.end()) ctype = ct->second;

                                bool isHead = (_req.method == "HEAD");
                                if (isHead)
                                {
                                    makeResponseHeaders(cgi.status, cgi.reason, ctype, 0, "", "");
                                    _state = WRITE;
                                }
                                else
                                {
                                    makeChunkedResponse(cgi.status, cgi.reason, ctype, cgi.body, "");
                                }
                                return;
                            }
                        }

                        {
                            StaticResult res;
                            if (StaticHandler::handleGET(*m.server, m.location, _req, res))
                            {
                                if (res.status == 404) { makeErrorWithPages(404, m.server); return; }
                                if (_req.method == "HEAD") res.body.clear();
                                makeResponseHeaders(res.status, res.reason, res.contentType,
                                                    res.body.size(), res.location, res.extraHeaders);
                                _out += res.body;
                                _state = WRITE;
                                return;
                            }
                        }

                        if (_req.method == "DELETE")
                        {
                            int code = ws::handleDelete(m, _req);
                            if (code == 0)   { makeErrorWithPages(500, m.server); return; }
                            if (code == 204) { _out = ws::buildHeaders(204, "No Content", "text/plain; charset=utf-8", 0, _curKeepAlive, "", ws::httpDateNow(), ""); _state = WRITE; return; }
                            if (code == 403) { makeResponse(403, "Forbidden", "text/plain; charset=utf-8", "403 Forbidden\n"); return; }
                            if (code == 404) { makeResponse(404, "Not Found", "text/plain; charset=utf-8", "404 Not Found\n"); return; }
                            if (code == 500) { makeResponse(500, "Internal Server Error", "text/plain; charset=utf-8", "500 Internal Server Error\n"); return; }
                        }

                        {
                            std::string echo = "Method: " + _req.method + "\nTarget: " + _req.target + "\nVersion: " + _req.version + "\n";
                            if (_req.hasHeader("host")) echo += "Host: " + _req.getHeader("host") + "\n";
                            if (!_req.body.empty())      echo += "Body-Bytes: " + itoa10((int)_req.body.size()) + "\n";
                            makeResponse(200, "OK", "text/plain; charset=utf-8", echo);
                            return;
                        }
                    }

                    if (r == HttpParser::BAD_REQUEST)      { makeErrorWithPages(400, defSrv); return; }
                    if (r == HttpParser::NOT_IMPLEMENTED)  { makeErrorWithPages(501, defSrv); return; }
                    if (r == HttpParser::LENGTH_REQUIRED)  { makeErrorWithPages(411, defSrv); return; }
                    if (r == HttpParser::ENTITY_TOO_LARGE) { makeErrorWithPages(413, defSrv); return; }
                }
                continue;
            }

            if (n == 0)
            {
                if (_state == WRITE && !_out.empty()) return;
                closeNow();
                return;
            }
            ws::Log::warn("recv() error, closing");
            closeNow();
            return;
        }
    }

    void Connection::onWritable()
    {
        if (_state != WRITE) return;
        while (!_out.empty())
        {
            ssize_t n = ::send(_fd, _out.data(), _out.size(), 0);
            if (n > 0) { _out.erase(0, (size_t)n); continue; }
            if (n < 0) return;
            ws::Log::warn("send() error, closing");
            closeNow();
            return;
        }
        if (_curKeepAlive)
        {
            _reqsOnConn++;
            _parser.reset();
            _out.clear();
            _state = READ;
            return;
        }
        closeNow();
    }

} // namespace ws