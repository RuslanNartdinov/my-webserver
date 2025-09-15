#ifndef WEBSERV_HTTP_STATIC_HANDLER_HPP
#define WEBSERV_HTTP_STATIC_HANDLER_HPP

#include "webserv/http/Request.hpp"
#include "webserv/config/Config.hpp"
#include <string>

namespace ws
{

	struct StaticResult
	{
		int status;			// 200, 301, 403, 404, 500
		std::string reason; // "OK", "Moved Permanently", ...
		std::string contentType;
		std::string body;
		std::string location; // для редиректов
		size_t contentLength;
	};

	class StaticHandler
	{
	public:
		// Вернёт true, если сформирован ответ (в StaticResult)
		// base: server + (optional) location; requestTarget — как в HTTP (включая '?', мы отрежем)
		static bool handleGET(const ServerConfig &srv,
							  const Location *loc,
							  const HttpRequest &req,
							  StaticResult &out);

	private:
		static std::string pathOnly(const std::string &target);
		static bool isDir(const std::string &fsPath);
		static bool isFile(const std::string &fsPath);
		static bool readFile(const std::string &fsPath, std::string &out);
		static std::string dirListingHtml(const std::string &reqPath, const std::string &fsDir);
		static std::string mapToFsPath(const ServerConfig &srv, const Location *loc, const std::string &reqPath, bool &ok);
	};

} // namespace ws
#endif