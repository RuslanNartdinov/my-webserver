#include "webserv/http/StaticHandler.hpp"
#include "webserv/fs/Path.hpp"
#include "webserv/utils/Mime.hpp"
#include "webserv/config/Config.hpp" // ServerConfig, Location
#include "webserv/http/Request.hpp"	 // HttpRequest

#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <ctime>
#include <time.h>

namespace ws
{

	// ---------- helpers: reason + http date (RFC 7231 IMF-fixdate) ----------

	static std::string httpDate(time_t t)
	{
		char buf[64];
#if defined(_WIN32)
		struct tm g;
		gmtime_s(&g, &t);
#else
		struct tm g;
		gmtime_r(&t, &g);
#endif
		strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &g);
		return std::string(buf);
	}

	static std::string makeWeakETag(off_t sz, time_t mt)
	{
		// формат ровно как в тесте: "W/<size>-<mtime>"
		std::ostringstream oss;
		oss << "\"W/" << (unsigned long long)sz << "-" << (unsigned long long)mt << "\"";
		return oss.str();
	}

	static std::string reasonFor(int code)
	{
		switch (code)
		{
		case 200:
			return "OK";
		case 301:
			return "Moved Permanently";
		case 304:
			return "Not Modified";
		case 403:
			return "Forbidden";
		case 404:
			return "Not Found";
		case 500:
			return "Internal Server Error";
		}
		return "OK";
	}

	static std::string httpDateFromTime(std::time_t t)
	{
		char buf[64];
		std::tm gmt;
#if defined(_WIN32)
		gmtime_s(&gmt, &t);
#else
		gmtime_r(&t, &gmt);
#endif
		std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
		return std::string(buf);
	}

	// Поддержим основную форму IMF-fixdate: "Tue, 15 Nov 1994 08:12:31 GMT"
	// Возврат: -1 если не распарсили
	static std::time_t parseHttpDate(const std::string &s)
	{
		if (s.size() < 29)
			return (time_t)-1;
		std::tm tmv;
		memset(&tmv, 0, sizeof(tmv));
		char *res = strptime(s.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tmv);
		if (!res || *res != '\0')
			return (time_t)-1;
#if defined(_WIN32)
		return (time_t)-1;
#else
		return timegm(&tmv);
#endif
	}

	// -------------------- утилиты --------------------

	std::string StaticHandler::pathOnly(const std::string &target)
	{
		size_t q = target.find('?');
		return q == std::string::npos ? target : target.substr(0, q);
	}

	bool StaticHandler::isDir(const std::string &p)
	{
		struct stat st;
		if (stat(p.c_str(), &st) != 0)
			return false;
		return S_ISDIR(st.st_mode);
	}

	bool StaticHandler::isFile(const std::string &p)
	{
		struct stat st;
		if (stat(p.c_str(), &st) != 0)
			return false;
		return S_ISREG(st.st_mode);
	}

	bool StaticHandler::readFile(const std::string &fsPath, std::string &out)
	{
		std::ifstream ifs(fsPath.c_str(), std::ios::in | std::ios::binary);
		if (!ifs)
			return false;
		std::ostringstream oss;
		oss << ifs.rdbuf();
		out = oss.str();
		return true;
	}

	std::string StaticHandler::dirListingHtml(const std::string &reqPath, const std::string &fsDir)
	{
		std::ostringstream html;
		html << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of "
			 << reqPath << "</title></head><body><h1>Index of " << reqPath << "</h1><ul>";
		DIR *d = opendir(fsDir.c_str());
		if (d)
		{
			struct dirent *e;
			while ((e = readdir(d)))
			{
				const char *name = e->d_name;
				if (name[0] == '.')
					continue;
				html << "<li><a href=\"" << name << "\">" << name << "</a></li>";
			}
			closedir(d);
		}
		html << "</ul></body></html>";
		return html.str();
	}

	std::string StaticHandler::mapToFsPath(const ServerConfig &srv, const Location *loc,
										   const std::string &reqPath, bool &ok)
	{
		ok = true;

		std::string base;
		std::string rest = reqPath;

		if (loc)
		{
			// Нормализуем location.path
			std::string lpath = loc->path;
			if (!lpath.empty() && lpath[0] != '/')
				lpath = "/" + lpath;

			// Требуем совпадение по границе: либо точное совпадение, либо следующий символ — '/'
			const bool hasPrefix = (rest.size() >= lpath.size() &&
									rest.compare(0, lpath.size(), lpath) == 0 &&
									(rest.size() == lpath.size() || rest[lpath.size()] == '/'));

			if (hasPrefix)
			{
				std::string tail = rest.substr(lpath.size());
				if (!tail.empty() && tail[0] == '/')
					tail.erase(0, 1);

				if (!loc->alias.empty())
					base = loc->alias;
				else if (!loc->root.empty())
					base = loc->root;
				else
					base = srv.root.empty() ? "." : srv.root;

				rest = tail;
			}
			else
			{
				// Нет совпадения префикса — fallback к корню сервера
				base = srv.root.empty() ? "." : srv.root;
				if (!rest.empty() && rest[0] == '/')
					rest.erase(0, 1);
			}
		}
		else
		{
			// Нет подходящего location — используем server.root
			base = srv.root.empty() ? "." : srv.root;
			if (!rest.empty() && rest[0] == '/')
				rest.erase(0, 1);
		}

		std::string joined = pathJoin(base, rest);
		std::string norm = normalizePath("/" + joined);
		std::string normBase = normalizePath("/" + base);

		// Безопасность: конечный путь обязан «лежать под» base
		if (!startsWithPath(normBase, norm))
		{
			ok = false;
			return std::string();
		}

		// Возвращаем путь без ведущего '/'
		if (!norm.empty() && norm[0] == '/')
			return norm.substr(1);
		return norm;
	}

	static bool etagMatches(const std::string &inm, const std::string &etagQuotedW)
	{
		if (inm == etagQuotedW)
			return true; // "\"W/size-mtime\""
		// альтернативная форма: W/"size-mtime"
		if (etagQuotedW.size() >= 4 && etagQuotedW.compare(0, 3, "\"W/") == 0)
		{
			std::string core = etagQuotedW.substr(3, etagQuotedW.size() - 4); // size-mtime
			std::string alt = "W/\"" + core + "\"";
			if (inm == alt)
				return true;
		}
		return false;
	}

	bool StaticHandler::handleGET(const ServerConfig &srv,
								  const Location *loc,
								  const HttpRequest &req,
								  StaticResult &out)
	{
		std::string raw = req.getRawTarget().empty() ? req.target : req.getRawTarget();
		std::string reqPath = pathOnly(raw);
		if (ws::pathTraversalSuspect(reqPath))
		{
			out.status = 403;
			out.reason = "Forbidden";
			out.body = "403 Forbidden\n";
			out.contentType = "text/plain; charset=utf-8";
			out.contentLength = out.body.size();
			out.extraHeaders.clear();
			return true;
		}
		if (reqPath.empty())
			reqPath = "/";

		bool okMap = false;
		std::string fsPath = mapToFsPath(srv, loc, reqPath, okMap);
		if (!okMap)
		{
			out.status = 403;
			out.reason = reasonFor(403);
			out.body = "403 Forbidden\n";
			out.contentType = "text/plain; charset=utf-8";
			out.contentLength = out.body.size();
			out.extraHeaders.clear();
			return true;
		}

		bool wantDir = !reqPath.empty() && reqPath[reqPath.size() - 1] == '/';

		// ---------- FILE ----------
		if (isFile(fsPath) && !wantDir)
		{
			struct stat st;
			if (stat(fsPath.c_str(), &st) != 0)
			{
				out.status = 500;
				out.reason = reasonFor(500);
				out.body = "500 Internal Server Error\n";
				out.contentType = "text/plain; charset=utf-8";
				out.contentLength = out.body.size();
				return true;
			}

			std::string etag = makeWeakETag(st.st_size, st.st_mtime);
			std::string lastMod = httpDate(st.st_mtime);

			// If-None-Match → 304
			{
				std::string inm = req.getHeader("if-none-match");
				if (!inm.empty() && etagMatches(inm, etag))
				{
					out.status = 304;
					out.reason = "Not Modified";
					out.body.clear();
					out.contentType = mimeByExt(fsPath);
					out.contentLength = 0;
					out.extraHeaders.clear();
					out.extraHeaders += "ETag: " + etag + "\r\n";
					out.extraHeaders += "Last-Modified: " + lastMod + "\r\n";
					return true;
				}
			}

			// If-Modified-Since → 304
			{
				std::string ims = req.getHeader("if-modified-since");
				if (!ims.empty())
				{
					std::time_t ims_t = parseHttpDate(ims);
					if (ims_t != (time_t)-1 && st.st_mtime <= ims_t)
					{
						out.status = 304;
						out.reason = reasonFor(304);
						out.body.clear();
						out.contentType = mimeByExt(fsPath);
						out.contentLength = 0;
						out.extraHeaders.clear();
						out.extraHeaders += "ETag: " + etag + "\r\n";
						out.extraHeaders += "Last-Modified: " + lastMod + "\r\n";
						return true;
					}
				}
			}

			std::string body;
			if (!readFile(fsPath, body))
			{
				out.status = 500;
				out.reason = reasonFor(500);
				out.body = "500 Internal Server Error\n";
				out.contentType = "text/plain; charset=utf-8";
				out.contentLength = out.body.size();
				return true;
			}

			out.status = 200;
			out.reason = reasonFor(200);
			out.body = body;
			out.contentType = mimeByExt(fsPath); // ← FIX: fsPath, не cand
			out.contentLength = out.body.size();
			out.location.clear();
			out.extraHeaders.clear();
			out.extraHeaders += "ETag: " + etag + "\r\n";
			out.extraHeaders += "Last-Modified: " + lastMod + "\r\n";
			if (req.method == "HEAD")
			{
				out.body.clear();
				out.contentLength = 0;
			}
			return true;
		}

		// ---------- DIR ----------
		if (isDir(fsPath) || wantDir)
		{
			// индексные файлы: используем loc->index
			if (loc && !loc->index.empty())
			{
				for (size_t i = 0; i < loc->index.size(); ++i)
				{
					std::string cand = pathJoin(fsPath, loc->index[i]);
					if (isFile(cand))
					{
						struct stat st;
						std::string body;
						if (stat(cand.c_str(), &st) != 0 || !readFile(cand, body))
						{
							out.status = 500;
							out.reason = reasonFor(500);
							out.body = "500 Internal Server Error\n";
							out.contentType = "text/plain; charset=utf-8";
							out.contentLength = out.body.size();
							out.location.clear();
							out.extraHeaders.clear();
							return true;
						}

						std::time_t mtime = st.st_mtime;
						std::string lastMod = httpDateFromTime(mtime);
						std::string etag = makeWeakETag(st.st_size, st.st_mtime);

						// If-None-Match → 304
						{
							std::string inm = req.getHeader("if-none-match");
							if (!inm.empty() && etagMatches(inm, etag))
							{
								out.status = 304;
								out.reason = "Not Modified";
								out.body.clear();
								out.contentType = mimeByExt(cand);
								out.contentLength = 0;
								out.location.clear();
								out.extraHeaders.clear();
								out.extraHeaders += "ETag: " + etag + "\r\n";
								out.extraHeaders += "Last-Modified: " + lastMod + "\r\n";
								return true;
							}
						}

						// If-Modified-Since → 304
						{
							std::string ims = req.getHeader("if-modified-since");
							if (!ims.empty())
							{
								std::time_t ims_t = parseHttpDate(ims);
								if (ims_t != (time_t)-1 && mtime <= ims_t)
								{
									out.status = 304;
									out.reason = reasonFor(304);
									out.body.clear();
									out.contentType = mimeByExt(cand);
									out.contentLength = 0;
									out.location.clear();
									out.extraHeaders.clear();
									out.extraHeaders += "ETag: " + etag + "\r\n";
									out.extraHeaders += "Last-Modified: " + lastMod + "\r\n";
									return true;
								}
							}
						}

						// 200 OK
						out.status = 200;
						out.reason = reasonFor(200);
						out.body = body;
						out.contentType = mimeByExt(cand); // ← FIX: cand
						out.contentLength = out.body.size();
						out.location.clear();
						out.extraHeaders.clear();
						out.extraHeaders += "ETag: " + etag + "\r\n";
						out.extraHeaders += "Last-Modified: " + lastMod + "\r\n";
						if (req.method == "HEAD")
						{
							out.body.clear();
							out.contentLength = 0;
						}
						return true;
					}
				}
			}

			// если нет index и URL без завершающего '/', делаем 301 → вариант с '/'
			if (!wantDir)
			{
				out.status = 301;
				out.reason = reasonFor(301);
				std::string locHdr = reqPath + "/";
				out.location = locHdr;
				out.body.clear();
				out.contentType = "text/plain; charset=utf-8";
				out.contentLength = 0;
				out.extraHeaders.clear();
				return true;
			}

			// autoindex?
			bool ai = (loc ? loc->autoindex : false);
			if (ai)
			{
				std::string html = dirListingHtml(reqPath, fsPath);
				out.status = 200;
				out.reason = reasonFor(200);
				out.body = html;
				out.contentType = "text/html; charset=utf-8";
				out.contentLength = out.body.size();
				out.location.clear();
				out.extraHeaders.clear();
				return true;
			}

			// без autoindex — 403
			out.status = 403;
			out.reason = reasonFor(403);
			out.body = "403 Forbidden\n";
			out.contentType = "text/plain; charset=utf-8";
			out.contentLength = out.body.size();
			out.location.clear();
			out.extraHeaders.clear();
			return true;
		}

		// ---------- NOT FOUND ----------
		out.status = 404;
		out.reason = reasonFor(404);
		out.body = "404 Not Found\n";
		out.contentType = "text/plain; charset=utf-8";
		out.contentLength = out.body.size();
		out.location.clear();
		out.extraHeaders.clear();
		return true;
	}

} // namespace ws