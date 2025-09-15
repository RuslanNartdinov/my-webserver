#include "webserv/http/StaticHandler.hpp"
#include "webserv/fs/Path.hpp"
#include "webserv/utils/Mime.hpp"

#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <fstream>
#include <sstream>

namespace ws
{

	// ============ helpers ============

	static std::string reasonFor(int code)
	{
		switch (code)
		{
		case 200:
			return "OK";
		case 301:
			return "Moved Permanently";
		case 403:
			return "Forbidden";
		case 404:
			return "Not Found";
		case 500:
			return "Internal Server Error";
		}
		return "OK";
	}

	std::string StaticHandler::pathOnly(const std::string &target)
	{
		size_t q = target.find('?');
		return q == std::string::npos ? target : target.substr(0, q);
	}

	static bool isDirFS(const std::string &p)
	{
		struct stat st;
		if (stat(p.c_str(), &st) != 0)
			return false;
		return S_ISDIR(st.st_mode);
	}

	static bool isFileFS(const std::string &p)
	{
		struct stat st;
		if (stat(p.c_str(), &st) != 0)
			return false;
		return S_ISREG(st.st_mode);
	}

	bool StaticHandler::isDir(const std::string &p) { return isDirFS(p); }
	bool StaticHandler::isFile(const std::string &p) { return isFileFS(p); }

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

	// HTML автоиндекса (добавляет ../ и / для директорий)
	std::string StaticHandler::dirListingHtml(const std::string &reqPath, const std::string &fsDir)
	{
		std::ostringstream html;
		html << "<!doctype html><html><head><meta charset=\"utf-8\">"
			 << "<title>Index of " << reqPath << "</title></head><body>"
			 << "<h1>Index of " << reqPath << "</h1><hr><pre>";

		// ссылка на родителя
		if (reqPath != "/")
			html << "<a href=\"../\">../</a>\n";

		DIR *d = opendir(fsDir.c_str());
		if (d)
		{
			struct dirent *e;
			while ((e = readdir(d)))
			{
				const char *name = e->d_name;
				if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
					continue; // пропускаем . и ..

				std::string childFs = fsDir;
				if (!childFs.empty() && childFs[childFs.size() - 1] != '/')
					childFs += "/";
				childFs += name;

				bool isDir = isDirFS(childFs);

				std::string href = reqPath;
				if (href.empty() || href[href.size() - 1] != '/')
					href += "/";
				href += name;
				if (isDir)
					href += "/";

				html << "<a href=\"" << href << "\">" << name;
				if (isDir)
					html << "/";
				html << "</a>\n";
			}
			closedir(d);
		}
		html << "</pre><hr></body></html>";
		return html.str();
	}

	// Отображение HTTP-пути -> путь в ФС с безопасностью
	std::string StaticHandler::mapToFsPath(const ServerConfig &srv, const Location *loc,
										   const std::string &reqPath, bool &ok)
	{
		ok = true;
		// База: alias или root (loc.root || srv.root)
		std::string base;
		std::string rest = reqPath;

		if (loc && !loc->alias.empty())
		{
			// alias: заменяет локационный префикс на alias
			std::string lpath = loc->path;
			if (!lpath.empty() && lpath[0] != '/')
				lpath = "/" + lpath;

			if (lpath.size() <= rest.size() && rest.compare(0, lpath.size(), lpath) == 0)
			{
				std::string tail = rest.substr(lpath.size());
				if (!tail.empty() && tail[0] == '/')
					tail.erase(0, 1);
				base = loc->alias;
				rest = tail;
			}
			else
			{ // на всякий случай
				base = srv.root.empty() ? "." : srv.root;
				if (!rest.empty() && rest[0] == '/')
					rest.erase(0, 1);
			}
		}
		else
		{
			base = (loc && !loc->root.empty()) ? loc->root : (srv.root.empty() ? "." : srv.root);
			if (!rest.empty() && rest[0] == '/')
				rest.erase(0, 1);
		}

		// Склеим и нормализуем
		std::string joined = pathJoin(base, rest);
		std::string norm = normalizePath("/" + joined); // абсолютный вид
		std::string normBase = normalizePath("/" + base);

		// безопасность: итоговый путь должен начинаться с normBase (как префикс-путь)
		if (!startsWithPath(normBase, norm))
		{
			ok = false;
			return std::string();
		}

		// вернём без ведущего "/" как путь в ФС
		if (!norm.empty() && norm[0] == '/')
			return norm.substr(1);
		return norm;
	}

	// ============ основной обработчик статики ============

	bool StaticHandler::handleGET(const ServerConfig &srv,
								  const Location *loc,
								  const HttpRequest &req,
								  StaticResult &out)
	{
		// 1) базовая проверка и защита
		std::string raw = req.getRawTarget().empty() ? req.target : req.getRawTarget();
		std::string reqPath = pathOnly(raw);

		if (ws::pathTraversalSuspect(reqPath))
		{
			out.status = 403;
			out.reason = "Forbidden";
			out.body = "403 Forbidden\n";
			out.contentType = "text/plain; charset=utf-8";
			out.contentLength = out.body.size();
			return true;
		}
		if (reqPath.empty())
			reqPath = "/";

		// 2) маппинг
		bool okMap = false;
		std::string fsPath = mapToFsPath(srv, loc, reqPath, okMap);
		if (!okMap)
		{
			out.status = 403;
			out.reason = reasonFor(403);
			out.body = "403 Forbidden\n";
			out.contentType = "text/plain; charset=utf-8";
			out.contentLength = out.body.size();
			return true;
		}

		// 3) если явно просили путь со слешем — это директория
		bool wantDir = !reqPath.empty() && reqPath[reqPath.size() - 1] == '/';

		// 4) обычный файл
		if (isFileFS(fsPath) && !wantDir)
		{
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
			out.contentType = mimeByExt(fsPath);
			out.contentLength = out.body.size();
			return true;
		}

		// 5) директория или запрос на директорию
		// 5) директория или запрос на директорию
		if (isDirFS(fsPath) || wantDir)
		{
			// 5.1) если URL не оканчивается на '/', делаем 301 на вариант со слешем
			if (!wantDir)
			{
				out.status = 301;
				out.reason = reasonFor(301);
				out.location = reqPath + "/";
				out.body = "";
				out.contentType = "text/plain; charset=utf-8";
				out.contentLength = 0;
				return true;
			}

			// 5.2) пробуем index-файлы (только из location)
			if (loc && !loc->index.empty())
			{
				for (size_t i = 0; i < loc->index.size(); ++i)
				{
					std::string cand = pathJoin(fsPath, loc->index[i]);
					if (isFileFS(cand))
					{
						std::string body;
						if (!readFile(cand, body))
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
						out.contentType = mimeByExt(cand);
						out.contentLength = out.body.size();
						return true;
					}
				}
			}

			// 5.3) autoindex (только из location)
			bool ai = (loc ? loc->autoindex : false);
			if (ai)
			{
				std::string html = dirListingHtml(reqPath, fsPath);
				out.status = 200;
				out.reason = reasonFor(200);
				out.body = html;
				out.contentType = "text/html; charset=utf-8";
				out.contentLength = out.body.size();
				return true;
			}

			// 5.4) без autoindex — 403
			out.status = 403;
			out.reason = reasonFor(403);
			out.body = "403 Forbidden\n";
			out.contentType = "text/plain; charset=utf-8";
			out.contentLength = out.body.size();
			return true;
		}

		// 6) не найдено
		out.status = 404;
		out.reason = reasonFor(404);
		out.body = "404 Not Found\n";
		out.contentType = "text/plain; charset=utf-8";
		out.contentLength = out.body.size();
		return true;
	}

} // namespace ws