#include "webserv/http/Cgi.hpp"
#include "webserv/fs/Path.hpp"
#include "webserv/Log.hpp"
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

namespace ws
{

	static bool endsWith(const std::string &s, const std::string &suf)
	{
		return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
	}

	// сопоставление HTTP-пути -> относительный путь в ФС (как в статику)
	static bool mapToFsRel(const ServerConfig &srv, const Location *loc,
						   const std::string &reqPath, std::string &fsRelOut)
	{
		std::string base;
		std::string rest = reqPath;
		if (!rest.empty() && rest[0] == '/')
			rest.erase(0, 1);

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
				base = srv.root.empty() ? "." : srv.root;
			}
		}
		else
		{
			base = (loc && !loc->root.empty()) ? loc->root : (srv.root.empty() ? "." : srv.root);
		}

		std::string joined = ws::pathJoin(base, rest);		  // examples/site + cgi/hello.py
		std::string norm = ws::normalizePath("/" + joined);	  // /examples/site/cgi/hello.py
		std::string normBase = ws::normalizePath("/" + base); // /examples/site

		// безопасность: под базой?
		if (norm.size() < normBase.size() || norm.compare(0, normBase.size(), normBase) != 0 || (norm.size() > normBase.size() && norm[normBase.size()] != '/'))
		{
			return false;
		}
		if (!norm.empty() && norm[0] == '/')
			norm.erase(0, 1); // вернём относительный: examples/site/cgi/hello.py
		fsRelOut = norm;
		return true;
	}

	// разбор CGI-вывода: заголовки (до пустой строки) + тело
	static void parseCgiResponse(const std::string &out, CgiResult &res)
	{
		// 1) Найти разделитель заголовков и тела: сначала CRLFCRLF, потом LFLF
		size_t sep = out.find("\r\n\r\n");
		size_t off = 4;
		if (sep == std::string::npos)
		{
			sep = out.find("\n\n");
			off = 2;
		}

		std::string hdrs = (sep == std::string::npos) ? out : out.substr(0, sep);
		std::string body = (sep == std::string::npos) ? std::string() : out.substr(sep + off);

		// 2) Разбить заголовки по строкам: поддерживаем и CRLF, и LF
		size_t pos = 0;
		while (pos < hdrs.size())
		{
			// ищем конец строки: сначала CRLF, затем LF
			size_t e = hdrs.find("\r\n", pos);
			size_t line_end_len = 2;
			if (e == std::string::npos)
			{
				e = hdrs.find('\n', pos);
				line_end_len = 1;
			}
			if (e == std::string::npos)
				e = hdrs.size();

			std::string line = hdrs.substr(pos, e - pos);
			pos = (e == hdrs.size() ? e : e + line_end_len);

			if (line.empty())
				continue;

			// 3) Разобрать "Key: value"
			size_t c = line.find(':');
			if (c == std::string::npos)
				continue;

			std::string k = line.substr(0, c);
			for (size_t i = 0; i < k.size(); ++i)
			{
				char ch = k[i];
				if (ch >= 'A' && ch <= 'Z')
					k[i] = (char)(ch - 'A' + 'a');
			}

			std::string v = line.substr(c + 1);
			// trim пробелы слева
			while (!v.empty() && (v[0] == ' ' || v[0] == '\t'))
				v.erase(0, 1);

			if (k == "status")
			{
				// "200 OK" или "200"
				int code = 200;
				if (!v.empty())
					code = std::atoi(v.c_str());
				res.status = code;
				size_t sp = v.find(' ');
				if (sp != std::string::npos)
					res.reason = v.substr(sp + 1);
				else
					res.reason = "OK";
			}
			else
			{
				res.headers[k] = v;
			}
		}

		// 4) Если CGI не задал Content-Type — поставим дефолт
		if (res.headers.find("content-type") == res.headers.end())
		{
			res.headers["content-type"] = "text/html; charset=utf-8";
		}

		res.body = body;
	}

	static void setEnvKV(std::vector<std::string> &envs, const std::string &k, const std::string &v)
	{
		std::string kv = k + "=" + v;
		envs.push_back(kv);
	}

	static char **buildCStringArray(const std::vector<std::string> &vec, std::vector<char *> &store)
	{
		store.clear();
		for (size_t i = 0; i < vec.size(); ++i)
			store.push_back(const_cast<char *>(vec[i].c_str()));
		store.push_back(NULL);
		return &store[0];
	}

	bool CgiHandler::handle(const ServerConfig &srv,
							const Location *loc,
							const HttpRequest &req,
							CgiResult &out)
	{
		// location должен объявлять cgi_ext и cgi_bin
		if (!(loc && !loc->cgi_ext.empty() && !loc->cgi_bin.empty()))
			return false;

		// расширение совпадает?
		std::string path = req.target;
		size_t q = path.find('?');
		if (q != std::string::npos)
			path = path.substr(0, q);
		if (!endsWith(path, loc->cgi_ext))
			return false;

		// мапим в относительный путь ФС
		std::string fsRel;
		if (!mapToFsRel(srv, loc, path, fsRel))
		{
			out.status = 403;
			out.reason = "Forbidden";
			out.body = "403 Forbidden\n";
			return true;
		}

		// Подготовка пайпов
		int inPipe[2], outPipe[2];
		if (pipe(inPipe) != 0 || pipe(outPipe) != 0)
		{
			out.status = 500;
			out.reason = "Internal Server Error";
			out.body = "500 Internal Server Error\n";
			return true;
		}

		pid_t pid = fork();
		if (pid < 0)
		{
			close(inPipe[0]);
			close(inPipe[1]);
			close(outPipe[0]);
			close(outPipe[1]);
			out.status = 500;
			out.reason = "Internal Server Error";
			out.body = "500 Internal Server Error\n";
			return true;
		}

		if (pid == 0)
		{
			// child
			(void)dup2(inPipe[0], STDIN_FILENO);
			(void)dup2(outPipe[1], STDOUT_FILENO);
			close(inPipe[1]);
			close(outPipe[0]);

			// env
			std::vector<std::string> envs;
			setEnvKV(envs, "GATEWAY_INTERFACE", "CGI/1.1");
			setEnvKV(envs, "SERVER_SOFTWARE", "webserv-dev");
			setEnvKV(envs, "SERVER_PROTOCOL", req.version.empty() ? "HTTP/1.1" : req.version);
			setEnvKV(envs, "REQUEST_METHOD", req.method);
			setEnvKV(envs, "SCRIPT_NAME", path);	  // логический путь
			setEnvKV(envs, "SCRIPT_FILENAME", fsRel); // относительный к CWD
			// QUERY_STRING
			{
				const std::string &tgt = req.target;
				size_t qq = tgt.find('?');
				setEnvKV(envs, "QUERY_STRING", (qq == std::string::npos) ? "" : tgt.substr(qq + 1));
			}
			// CONTENT_LENGTH/TYPE
			{
				std::map<std::string, std::string>::const_iterator it;
				it = req.headers.find("content-length");
				if (it != req.headers.end())
					setEnvKV(envs, "CONTENT_LENGTH", it->second);
				it = req.headers.find("content-type");
				if (it != req.headers.end())
					setEnvKV(envs, "CONTENT_TYPE", it->second);
			}
			// HOST
			{
				std::map<std::string, std::string>::const_iterator it = req.headers.find("host");
				if (it != req.headers.end())
					setEnvKV(envs, "HTTP_HOST", it->second);
			}

			// argv: [cgi_bin, script]
			std::vector<std::string> argv;
			argv.push_back(loc->cgi_bin);
			argv.push_back(fsRel);

			std::vector<char *> envpStore, argvStore;
			char **envp = buildCStringArray(envs, envpStore);
			char **argvp = buildCStringArray(argv, argvStore);

			execve(loc->cgi_bin.c_str(), argvp, envp);
			_exit(127); // если execve не удался
		}

		// parent
		close(inPipe[0]);  // нам нужен write в stdin ребёнка
		close(outPipe[1]); // нам нужен read из stdout ребёнка

		// тело запроса -> stdin CGI
		if (!req.body.empty())
		{
			const char *p = req.body.data();
			size_t left = req.body.size();
			while (left)
			{
				ssize_t n = write(inPipe[1], p, left);
				if (n <= 0)
					break;
				p += n;
				left -= (size_t)n;
			}
		}
		close(inPipe[1]);

		// читаем stdout CGI
		std::string cgiOut;
		char buf[4096];
		for (;;)
		{
			ssize_t n = read(outPipe[0], buf, sizeof(buf));
			if (n > 0)
			{
				cgiOut.append(buf, (size_t)n);
				continue;
			}
			break;
		}
		close(outPipe[0]);

		int status = 0;
		(void)waitpid(pid, &status, 0);

		// парсим CGI-ответ (заголовки + тело)
		parseCgiResponse(cgiOut, out);

		return true;
	}

} // namespace ws