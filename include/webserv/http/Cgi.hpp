#ifndef WS_HTTP_CGI_HPP
#define WS_HTTP_CGI_HPP

#include <string>
#include <map>
#include "webserv/http/Request.hpp"
#include "webserv/config/Config.hpp"

namespace ws
{

	struct CgiResult
	{
		int status;
		std::string reason;
		std::map<std::string, std::string> headers; // lower-case ключи
		std::string body;
		CgiResult() : status(200), reason("OK"), headers(), body() {}
	};

	class CgiHandler
	{
	public:
		// true: это был CGI-кандидат, ответ собран в out (даже если 4xx/5xx)
		// false: не CGI — пусть обрабатывает статика
		static bool handle(const ServerConfig &srv,
						   const Location *loc,
						   const HttpRequest &req,
						   CgiResult &out);
	};

} // namespace ws
#endif