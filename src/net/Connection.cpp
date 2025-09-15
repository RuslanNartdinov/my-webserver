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
#include <sys/stat.h> // stat, mkdir, S_ISREG
#include <fcntl.h>	  // open flags
#include <ctime>	  // time()
#include <time.h>
#include "webserv/http/Router.hpp"
#include "webserv/config/Config.hpp"
#include "webserv/http/StaticHandler.hpp"
#include "webserv/fs/Path.hpp"	// normalizePath, pathJoin
#include "webserv/http/Cgi.hpp" // CgiHandler, CgiResult

namespace ws
{
	// --- helpers / small utils ------------------------------------------------

	static std::string httpDate()
	{
		// Формат RFC 7231: Sun, 06 Nov 1994 08:49:37 GMT
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

	// дефолтный server для текущего listener (когда ещё нет Host / routing match)
	static const ServerConfig *pickDefaultServer(const Router *router,
												 const std::string &lhost,
												 int lport)
	{
		if (!router)
			return 0;
		RouteMatch tmp = router->resolve(lhost, lport, /*host*/ "", /*target*/ "/");
		return tmp.server; // может быть NULL
	}

	// Обрезать query-часть
	static std::string pathOnly(const std::string &target)
	{
		size_t q = target.find('?');
		return q == std::string::npos ? target : target.substr(0, q);
	}

	// Проверка «это обычный файл?»
	static bool isFileFS(const std::string &p)
	{
		struct stat st;
		if (stat(p.c_str(), &st) != 0)
			return false;
		return S_ISREG(st.st_mode);
	}

	// Безопасное отображение HTTP-пути в путь ФС для DELETE
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
			// alias: вырезаем префикс location->path
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
			// root: server.root либо location.root
			base = (loc && !loc->root.empty()) ? loc->root : (srv->root.empty() ? "." : srv->root);
			if (!rest.empty() && rest[0] == '/')
				rest.erase(0, 1);
		}

		std::string joined = ws::pathJoin(base, rest);
		std::string norm = ws::normalizePath("/" + joined);
		std::string normBase = ws::normalizePath("/" + base);

		// Безопасность: конечный путь должен «лежать под» base
		if (norm.size() < normBase.size() ||
			norm.compare(0, normBase.size(), normBase) != 0 ||
			(norm.size() > normBase.size() && norm[normBase.size()] != '/'))
		{
			ok = false;
			return std::string();
		}

		// Вернём путь без ведущего слеша как «путь в ФС»
		if (!norm.empty() && norm[0] == '/')
			return norm.substr(1);
		return norm;
	}

	// --- Connection methods ---------------------------------------------------

	bool Connection::shouldKeepAlive(const HttpRequest &r) const
	{
		// HTTP/1.1: keep-alive по умолчанию, кроме явного "Connection: close"
		// HTTP/1.0: keep-alive только при явном "Connection: keep-alive"
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
			ka = false; // мягкий лимит
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

						// выставляем флаг keep-alive для этого запроса
						_curKeepAlive = shouldKeepAlive(_req);

						// Роутинг
						RouteMatch m = _router->resolve(_lhost, _lport,
														_req.getHeader("host"),
														_req.target);

						// HEAD считается как GET для allow_methods
						std::string methodForCheck = _req.method;
						if (methodForCheck == "HEAD")
							methodForCheck = "GET";

						// allow_methods
						if (!methodAllowed(m.location, methodForCheck))
						{
							std::string allow = m.location ? joinCSV(m.location->allow_methods)
														   : std::string("GET, POST, DELETE");
							// если GET разрешён, укажем и HEAD
							if (allow.find("GET") != std::string::npos &&
								allow.find("HEAD") == std::string::npos)
								allow += ", HEAD";

							std::string extra = "Allow: " + allow + "\r\n";
							makeResponseHeaders(405, "Method Not Allowed",
												"text/plain; charset=utf-8",
												0, "", extra);
							return;
						}

						// Простой return (3xx) из location
						if (m.location && m.location->return_code >= 300 &&
							m.location->return_code < 400 && !m.location->return_url.empty())
						{
							makeResponse(m.location->return_code, "Moved Permanently",
										 "text/plain; charset=utf-8", "",
										 m.location->return_url);
							return;
						}

						// GET/POST/HEAD
						if (_req.method == "GET" || _req.method == "POST" || _req.method == "HEAD")
						{
							bool isHead = (_req.method == "HEAD");

							// 1) CGI сначала
							CgiResult cgi;
							if (m.location && m.server && CgiHandler::handle(*m.server, m.location, _req, cgi))
							{
								// Определим Content-Type
								std::string ctype = "text/html; charset=utf-8";
								std::map<std::string, std::string>::const_iterator ct = cgi.headers.find("content-type");
								if (ct != cgi.headers.end())
									ctype = ct->second;

								// Пробросим доп. заголовки от CGI (кроме уже учтённых)
								std::ostringstream extra;
								for (std::map<std::string, std::string>::const_iterator it = cgi.headers.begin();
									 it != cgi.headers.end(); ++it)
								{
									const std::string &k = it->first;
									if (k == "status" || k == "content-type" || k == "content-length")
										continue;
									extra << k << ": " << it->second << "\r\n";
								}

								// Если CGI НЕ указал Content-Length — шлём chunked (кроме HEAD)
								bool hasCL = (cgi.headers.find("content-length") != cgi.headers.end());
								if (!hasCL)
								{
									if (isHead)
									{
										// Тело не шлём — отдадим пустое тело с CL=0
										makeResponseHeaders(cgi.status, cgi.reason, ctype, 0, "", extra.str());
										_state = WRITE;
									}
									else
									{
										makeChunkedResponse(cgi.status, cgi.reason, ctype, cgi.body, extra.str());
									}
									return;
								}

								// Иначе — обычный ответ с Content-Length
								makeResponseHeaders(cgi.status, cgi.reason, ctype, cgi.body.size(), "", extra.str());
								if (!isHead)
									_out += cgi.body; // для HEAD тело не посылаем
								_state = WRITE;
								return;
							}

							// 2) Иначе — статика
							StaticResult res;
							if (StaticHandler::handleGET(*m.server, m.location, _req, res))
							{
								if (isHead)
								{
									// Только заголовки, без тела
									makeResponseHeaders(res.status, res.reason, res.contentType, res.contentLength, res.location, "");
								}
								else
								{
									makeResponse(res.status, res.reason, res.contentType, res.body, res.location);
								}
								return;
							}

							// Ни CGI, ни статика — 500
							makeErrorWithPages(500, m.server);
							return;
						}

						// DELETE
						if (_req.method == "DELETE")
						{
							const std::string &raw = _req.getRawTarget().empty() ? _req.target : _req.getRawTarget();

							// временные логи
							ws::Log::info(std::string("[DEL] raw='") + raw + "' target='" + _req.target + "'");

							std::string p = pathOnly(raw);
							ws::Log::info(std::string("[DEL] pathOnly(raw)='") + p + "'");

							if (ws::pathTraversalSuspect(p))
							{
								ws::Log::info("[DEL] traversalSuspect=TRUE -> 403");
								makeResponse(403, "Forbidden", "text/plain; charset=utf-8", "403 Forbidden\n");
								return;
							}
							else
							{
								ws::Log::info("[DEL] traversalSuspect=FALSE");
							}

							bool okMap = false;
							std::string fs = mapFsForDelete(m.server, m.location, p, okMap);
							ws::Log::info(std::string("[DEL] okMap=") + (okMap ? "true" : "false") + " fs='" + fs + "'");

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

						// fallback: временная заглушка
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

					// Ошибки разбора: используем дефолтный server, т.к. m ещё нет
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
				// Клиент закрыл свой write-end. Если ответ готов — дождёмся POLLOUT и отправим.
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

		// Ответ полностью отправлен
		if (_curKeepAlive)
		{
			// готовим соединение к следующему запросу
			_reqsOnConn++;
			_parser.reset(); // <— важно для keep-alive
			_out.clear();
			_state = READ;
			return;
		}

		// по умолчанию: закрываем
		closeNow();
	}

	void Connection::makeResponse(int code, const std::string &reason,
								  const std::string &ctype,
								  const std::string &body,
								  const std::string &location)
	{
		makeResponseHeaders(code, reason, ctype, body.size(), location, "");
		_out += body;
	}

	void Connection::makeResponseHeaders(int code, const std::string &reason,
										 const std::string &ctype, size_t clen,
										 const std::string &location,
										 const std::string &extra)
	{
		std::ostringstream oss;
		oss << "HTTP/1.1 " << code << " " << reason << "\r\n"
			<< "Server: webserv-dev\r\n"
			<< "Date: " << httpDate() << "\r\n"
			<< "Content-Type: " << ctype << "\r\n"
			<< "Content-Length: " << (int)clen << "\r\n"
			<< "Connection: " << (_curKeepAlive ? "keep-alive" : "close") << "\r\n";
		if (_curKeepAlive)
		{
			// Параметры — по вкусу; под это можно потом настроить таймаут в poll/epoll
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
				// путь относительный к корню сервера
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

	// --- chunked helper ------------------------------------------------------

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
		if (!extra.empty())
			oss << extra;
		oss << "\r\n";

		// один чанк + завершающий ноль
		oss << hexLower(body.size()) << "\r\n";
		oss << body << "\r\n";
		oss << "0\r\n\r\n";

		_out = oss.str();
		_state = WRITE;
	}

} // namespace ws