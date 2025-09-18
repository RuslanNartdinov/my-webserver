#include "webserv/net/Connection.hpp"
#include "webserv/Log.hpp"
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <poll.h>
#include <sstream>
#include <fstream>
#include <map>
#include <vector>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctime>
#include <time.h>
#include "webserv/http/Router.hpp"
#include "webserv/config/Config.hpp"
#include "webserv/http/StaticHandler.hpp"
#include "webserv/fs/Path.hpp"
#include "webserv/http/Cgi.hpp"

namespace ws
{
	static std::string httpDate()
	{
		char buf[64];
		std::time_t t = std::time(0);
		std::tm gmt;
#if defined(_WIN32)
		gmtime_s(&gmt, &t);
#else
		gmtime_r(&t, &gmt);
#endif
		std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
		return std::string(buf);
	}

	static bool readWholeFile(const std::string &p, std::string &out)
	{
		std::ifstream ifs(p.c_str(), std::ios::in | std::ios::binary);
		if (!ifs)
			return false;
		std::ostringstream oss;
		oss << ifs.rdbuf();
		out = oss.str();
		return true;
	}

	static std::string joinCSV(const std::vector<std::string> &v)
	{
		std::string s;
		for (size_t i = 0; i < v.size(); ++i)
		{
			if (i)
				s += ", ";
			s += v[i];
		}
		return s;
	}

	static std::string itoa10(int x)
	{
		std::ostringstream oss;
		oss << x;
		return oss.str();
	}

	static const ServerConfig *pickDefaultServer(const Router *router,
												 const std::string &lhost,
												 int lport)
	{
		if (!router)
			return 0;
		RouteMatch tmp = router->resolve(lhost, lport, "", "/");
		return tmp.server;
	}

	static std::string pathOnly(const std::string &target)
	{
		size_t q = target.find('?');
		return q == std::string::npos ? target : target.substr(0, q);
	}

	static bool isFileFS(const std::string &p)
	{
		struct stat st;
		if (stat(p.c_str(), &st) != 0)
			return false;
		return S_ISREG(st.st_mode);
	}

	static bool ensureDirRecursive(const std::string &dir)
	{
		if (dir.empty())
			return false;
		std::string cur;
		for (size_t i = 0; i < dir.size(); ++i)
		{
			char ch = dir[i];
			cur.push_back(ch);
			if (ch == '/' || i + 1 == dir.size())
			{
				if (cur == "/")
					continue;
				struct stat st;
				if (stat(cur.c_str(), &st) != 0)
				{
					if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST)
						return false;
				}
				else if (!S_ISDIR(st.st_mode))
				{
					return false;
				}
			}
		}
		return true;
	}

	static std::string genUploadName()
	{
		std::ostringstream oss;
		oss << "up_" << (unsigned long long)time(0)
			<< "_" << (unsigned long long)getpid()
			<< "_" << (unsigned long long)rand();
		return oss.str();
	}

	static bool writeBinary(const std::string &path, const std::string &data)
	{
		int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
		if (fd < 0)
			return false;
		const char *p = data.data();
		size_t left = data.size();
		while (left)
		{
			ssize_t n = ::write(fd, p, left);
			if (n < 0)
			{
				if (errno == EINTR)
					continue;
				::close(fd);
				return false;
			}
			p += n;
			left -= (size_t)n;
		}
		::close(fd);
		return true;
	}

	static std::string mapFsForDelete(const ws::ServerConfig *srv,
									  const ws::Location *loc,
									  const std::string &reqPathIn,
									  bool &ok)
	{
		ok = true;
		if (!srv)
		{
			ok = false;
			return std::string();
		}

		std::string reqPath = reqPathIn;
		std::string base;
		std::string rest = reqPath;

		if (loc && !loc->alias.empty())
		{
			std::string lpath = loc->path;
			if (!lpath.empty() && lpath[0] != '/')
				lpath = "/" + lpath;
			if (reqPath.size() >= lpath.size() && reqPath.compare(0, lpath.size(), lpath) == 0)
			{
				std::string tail = reqPath.substr(lpath.size());
				if (!tail.empty() && tail[0] == '/')
					tail.erase(0, 1);
				base = loc->alias;
				rest = tail;
			}
			else
			{
				base = srv->root.empty() ? "." : srv->root;
				if (!rest.empty() && rest[0] == '/')
					rest.erase(0, 1);
			}
		}
		else
		{
			base = (loc && !loc->root.empty()) ? loc->root : (srv->root.empty() ? "." : srv->root);
			if (!rest.empty() && rest[0] == '/')
				rest.erase(0, 1);
		}

		std::string joined = ws::pathJoin(base, rest);
		std::string norm = ws::normalizePath("/" + joined);
		std::string normBase = ws::normalizePath("/" + base);

		if (norm.size() < normBase.size() ||
			norm.compare(0, normBase.size(), normBase) != 0 ||
			(norm.size() > normBase.size() && norm[normBase.size()] != '/'))
		{
			ok = false;
			return std::string();
		}

		if (!norm.empty() && norm[0] == '/')
			return norm.substr(1);
		return norm;
	}

	bool Connection::shouldKeepAlive(const HttpRequest &r) const
	{
		std::string ver = r.version;
		std::string c = r.getHeader("connection");
		for (size_t i = 0; i < c.size(); ++i)
		{
			char ch = c[i];
			if (ch >= 'A' && ch <= 'Z')
				c[i] = char(ch - 'A' + 'a');
		}
		bool ka = false;
		if (ver == "HTTP/1.1")
			ka = (c != "close");
		else
			ka = (c == "keep-alive");
		if (_reqsOnConn >= MAX_KEEPALIVE)
			ka = false;
		return ka;
	}

	Connection::~Connection()
	{
		if (_fd >= 0)
			::close(_fd);
	}

	void Connection::closeNow()
	{
		if (_fd >= 0)
		{
			::close(_fd);
			_fd = -1;
		}
		_state = CLOSED;
	}

	short Connection::wantEvents() const
	{
		if (_state == READ)
			return POLLIN;
		if (_state == WRITE)
			return POLLOUT;
		return 0;
	}

	void Connection::makeError(int code, const std::string &text)
	{
		std::string body = itoa10(code) + " " + text + "\n";
		makeResponseHeaders(code, text, "text/plain; charset=utf-8", body.size(), "", "");
		_out += body;
		_state = WRITE;
	}

	void Connection::makeOkText(const std::string &text)
	{
		makeResponseHeaders(200, "OK", "text/plain; charset=utf-8", text.size(), "", "");
		_out += text;
		_state = WRITE;
	}

	void Connection::onReadable()
	{
		if (_state != READ)
			return;

		char buf[8192];
		for (;;)
		{
			ssize_t n = ::recv(_fd, buf, sizeof(buf), 0);
			if (n > 0)
			{
				_parser.feed(buf, (size_t)n);
				HttpParser::Result r;

				for (;;)
				{
					HttpRequest req;
					r = _parser.parse(req);
					if (r == HttpParser::NEED_MORE)
						break;

					if (r == HttpParser::OK)
					{
						_req = req;
						_curKeepAlive = shouldKeepAlive(_req);

						if (_req.version == "HTTP/1.1" && !_req.hasHeader("host"))
						{
							const ServerConfig *defSrv = pickDefaultServer(_router, _lhost, _lport);
							makeErrorWithPages(400, defSrv);
							return;
						}

						RouteMatch m = _router->resolve(_lhost, _lport,
														_req.getHeader("host"),
														_req.target);

						std::string methodForCheck = _req.method;
						if (methodForCheck == "HEAD")
							methodForCheck = "GET";

						{
							bool implemented =
								(_req.method == "GET" || _req.method == "POST" ||
								 _req.method == "DELETE" || _req.method == "HEAD");

							if (!implemented)
							{
								if (m.location && m.location->path == "/upload")
								{
									std::string allow = m.location ? joinCSV(m.location->allow_methods)
																   : std::string("GET, POST, DELETE");
									if (allow.find("GET") != std::string::npos &&
										allow.find("HEAD") == std::string::npos)
										allow += ", HEAD";

									std::string extra = "Allow: " + allow + "\r\n";
									makeResponseHeaders(405, "Method Not Allowed",
														"text/plain; charset=utf-8",
														0, "", extra);
									return;
								}
								const ServerConfig *defSrv = pickDefaultServer(_router, _lhost, _lport);
								makeErrorWithPages(501, defSrv);
								return;
							}
						}

						if (!methodAllowed(m.location, methodForCheck))
						{
							std::string allow = m.location ? joinCSV(m.location->allow_methods)
														   : std::string("GET, POST, DELETE");
							if (allow.find("GET") != std::string::npos &&
								allow.find("HEAD") == std::string::npos)
								allow += ", HEAD";

							std::string extra = "Allow: " + allow + "\r\n";
							makeResponseHeaders(405, "Method Not Allowed",
												"text/plain; charset=utf-8",
												0, "", extra);
							return;
						}

						if (_req.method != "GET" && _req.method != "POST" &&
							_req.method != "HEAD" && _req.method != "DELETE")
						{
							makeErrorWithPages(501, m.server);
							return;
						}

						if (m.location && m.location->return_code >= 300 &&
							m.location->return_code < 400 && !m.location->return_url.empty())
						{
							makeResponse(m.location->return_code, "Moved Permanently",
										 "text/plain; charset=utf-8", "",
										 m.location->return_url);
							return;
						}

						if (_req.method == "GET" || _req.method == "POST" || _req.method == "HEAD")
						{
							bool isHead = (_req.method == "HEAD");

							if (_req.method == "POST")
							{
								if (handlePostUpload(m))
									return;
							}

							CgiResult cgi;
							if (m.location && m.server && CgiHandler::handle(*m.server, m.location, _req, cgi))
							{
								std::string ctype = "text/html; charset=utf-8";
								std::map<std::string, std::string>::const_iterator ct = cgi.headers.find("content-type");
								if (ct != cgi.headers.end())
									ctype = ct->second;

								std::ostringstream extra;
								for (std::map<std::string, std::string>::const_iterator it = cgi.headers.begin();
									 it != cgi.headers.end(); ++it)
								{
									const std::string &k = it->first;
									if (k == "status" || k == "content-type" || k == "content-length")
										continue;
									extra << k << ": " << it->second << "\r\n";
								}

								bool hasCL = (cgi.headers.find("content-length") != cgi.headers.end());
								if (!hasCL)
								{
									if (isHead)
									{
										makeResponseHeaders(cgi.status, cgi.reason, ctype, 0, "", extra.str());
										_state = WRITE;
									}
									else
									{
										makeChunkedResponse(cgi.status, cgi.reason, ctype, cgi.body, extra.str());
									}
									return;
								}

								makeResponseHeaders(cgi.status, cgi.reason, ctype, cgi.body.size(), "", extra.str());
								if (!isHead)
									_out += cgi.body;
								_state = WRITE;
								return;
							}

							StaticResult res;
							if (StaticHandler::handleGET(*m.server, m.location, _req, res))
							{
								if (res.status == 404)
								{
									makeErrorWithPages(404, m.server);
									return;
								}

								if (isHead)
								{
                                    res.body.clear();
								}

								const size_t clen = res.body.size();
								makeResponseHeaders(res.status, res.reason,
													res.contentType, clen,
													res.location, res.extraHeaders);
								_out += res.body;
								_state = WRITE;
								return;
							}

							makeErrorWithPages(500, m.server);
							return;
						}

						if (_req.method == "DELETE")
						{
							const std::string &raw = _req.getRawTarget().empty() ? _req.target : _req.getRawTarget();
							std::string p = pathOnly(raw);

							if (ws::pathTraversalSuspect(p))
							{
								makeResponse(403, "Forbidden", "text/plain; charset=utf-8", "403 Forbidden\n");
								return;
							}

							bool okMap = false;
							std::string fs = mapFsForDelete(m.server, m.location, p, okMap);

							if (!okMap)
							{
								makeResponse(403, "Forbidden", "text/plain; charset=utf-8", "403 Forbidden\n");
								return;
							}
							if (!isFileFS(fs))
							{
								makeResponse(404, "Not Found", "text/plain; charset=utf-8", "404 Not Found\n");
								return;
							}
							if (::unlink(fs.c_str()) != 0)
							{
								makeResponse(500, "Internal Server Error", "text/plain; charset=utf-8", "500 Internal Server Error\n");
								return;
							}
							makeResponse(204, "No Content", "text/plain; charset=utf-8", "");
							return;
						}

						{
							std::string echo = "Method: " + _req.method + "\nTarget: " + _req.target + "\nVersion: " + _req.version + "\n";
							if (_req.hasHeader("host"))
								echo += "Host: " + _req.getHeader("host") + "\n";
							if (!_req.body.empty())
								echo += "Body-Bytes: " + itoa10((int)_req.body.size()) + "\n";
							makeOkText(echo);
							return;
						}
					}

					const ServerConfig *defSrv = pickDefaultServer(_router, _lhost, _lport);

					if (r == HttpParser::BAD_REQUEST)
					{
						makeErrorWithPages(400, defSrv);
						return;
					}
					if (r == HttpParser::NOT_IMPLEMENTED)
					{
						makeErrorWithPages(501, defSrv);
						return;
					}
					if (r == HttpParser::LENGTH_REQUIRED)
					{
						makeErrorWithPages(411, defSrv);
						return;
					}
					if (r == HttpParser::ENTITY_TOO_LARGE)
					{
						makeErrorWithPages(413, defSrv);
						return;
					}
				}
				continue;
			}

			if (n == 0)
			{
				if (_state == WRITE && !_out.empty())
					return;
				closeNow();
				return;
			}

			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;

			ws::Log::warn("recv() error, closing");
			closeNow();
			return;
		}
	}

	void Connection::onWritable()
	{
		if (_state != WRITE)
			return;

		while (!_out.empty())
		{
			ssize_t n = ::send(_fd, _out.data(), _out.size(), 0);
			if (n > 0)
			{
				_out.erase(0, (size_t)n);
				continue;
			}
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
				return;
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

	void Connection::makeResponse(int code, const std::string &reason,
								  const std::string &ctype,
								  const std::string &body,
								  const std::string &location)
	{
		const bool isHead = (_req.method == "HEAD");
		const size_t len = isHead ? 0 : body.size();
		makeResponseHeaders(code, reason, ctype, len, location, "");
		if (!isHead)
			_out += body;
	}

	void Connection::makeResponseHeaders(int code, const std::string &reason,
										 const std::string &ctype, size_t clen,
										 const std::string &location,
										 const std::string &extra)
	{
		_out.clear();
		std::ostringstream oss;
		oss << "HTTP/1.1 " << code << " " << reason << "\r\n"
			<< "Server: webserv-dev\r\n"
			<< "Date: " << httpDate() << "\r\n"
			<< "Content-Type: " << ctype << "\r\n"
			<< "Content-Length: " << (int)clen << "\r\n"
			<< "Connection: " << (_curKeepAlive ? "keep-alive" : "close") << "\r\n";
		if (_curKeepAlive)
		{
			oss << "Keep-Alive: timeout=5, max=100\r\n";
		}
		if (!location.empty())
			oss << "Location: " << location << "\r\n";
		if (!extra.empty())
			oss << extra;
		oss << "\r\n";
		_out = oss.str();
		_state = WRITE;
	}

	void Connection::makeErrorWithPages(int code, const ServerConfig *srv)
	{
		std::string reason = (code == 400 ? "Bad Request" : code == 411 ? "Length Required"
														: code == 413	? "Payload Too Large"
														: code == 501	? "Not Implemented"
																		: "Error");
		std::string body;
		std::string ctype = "text/plain; charset=utf-8";

		if (srv)
		{
			std::map<int, std::string>::const_iterator it = srv->error_pages.find(code);
			if (it != srv->error_pages.end())
			{
				std::string p = it->second;
				std::string fs = p;
				if (!fs.empty() && fs[0] == '.')
					fs = fs.substr(2);
				if (!srv->root.empty() && (p.size() < 2 || p.substr(0, 2) != "./"))
					fs = srv->root + "/" + p;

				if (readWholeFile(fs, body))
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

	static std::string hexLower(size_t x)
	{
		std::ostringstream oss;
		oss << std::hex << std::nouppercase << x;
		return oss.str();
	}

	void Connection::makeChunkedResponse(int code, const std::string &reason,
										 const std::string &ctype,
										 const std::string &body,
										 const std::string &extra)
	{
		std::ostringstream oss;
		oss << "HTTP/1.1 " << code << " " << reason << "\r\n";
		oss << "Server: webserv-dev\r\n";
		oss << "Content-Type: " << ctype << "\r\n";
		oss << "Transfer-Encoding: chunked\r\n";
		oss << "Connection: " << (_curKeepAlive ? "keep-alive" : "close") << "\r\n";
		if (_curKeepAlive)
			oss << "Keep-Alive: timeout=5, max=100\r\n";
		if (!extra.empty())
			oss << extra;
		oss << "\r\n";

		oss << hexLower(body.size()) << "\r\n";
		oss << body << "\r\n";
		oss << "0\r\n\r\n";

		_out = oss.str();
		_state = WRITE;
	}

	bool Connection::handlePostUpload(const RouteMatch &m)
	{
		if (_req.method != "POST" || !m.location || !m.location->upload_enable || m.location->upload_store.empty())
			return false;

		size_t limit = 10 * 1024 * 1024;
		if (m.location->client_max_body_size)
			limit = m.location->client_max_body_size;
		else if (m.server && m.server->client_max_body_size)
			limit = m.server->client_max_body_size;

		if (_req.body.size() > limit)
		{
			makeErrorWithPages(413, m.server);
			return true;
		}

		std::string serverRoot = (m.server && !m.server->root.empty()) ? m.server->root : std::string(".");
		std::string updir = m.location->upload_store;
		if (!updir.empty() && updir[0] != '/')
			updir = (serverRoot.back() == '/' ? serverRoot + updir : serverRoot + "/" + updir);

		if (!ensureDirRecursive(updir))
		{
			makeResponse(500, "Internal Server Error", "text/plain; charset=utf-8", "500 Internal Server Error\n");
			return true;
		}

		const std::string fileName = genUploadName();
		const std::string outPath = updir + "/" + fileName;

		if (!writeBinary(outPath, _req.body))
		{
			makeResponse(500, "Internal Server Error", "text/plain; charset=utf-8", "500 Internal Server Error\n");
			return true;
		}

		const std::string locationHdr = "/uploads/" + fileName;

		makeResponseHeaders(201, "Created",
							"text/plain; charset=utf-8",
							12,
							locationHdr,
							"");
		if (_req.method != "HEAD")
			_out += "201 Created\n";
		_state = WRITE;
		return true;
	}

} // namespace ws