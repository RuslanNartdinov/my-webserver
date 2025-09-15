#ifndef WEBSERV_HTTP_REQUEST_HPP
#define WEBSERV_HTTP_REQUEST_HPP

#include <string>
#include <map>
#include <vector>

namespace ws
{

	struct HttpRequest
	{
		// --- request line ---
		std::string method;	 // GET / POST / DELETE
		std::string target;	 // нормализованный путь (как раньше)
		std::string version; // HTTP/1.1

		// --- headers/body ---
		std::map<std::string, std::string> headers; // ключи в lower-case
		std::string body;							// уже dechuncked (если было chunked)

		// --- НОВОЕ: сырой request-target из первой строки (до нормализации) ---
		std::string raw_target;

		// helpers
		bool headerEquals(const std::string &name, const std::string &value) const;
		bool hasHeader(const std::string &name) const;
		std::string getHeader(const std::string &name) const;

		// НОВОЕ: удобный геттер с безопасным доступом
		const std::string &getRawTarget() const { return raw_target; }
	};

} // namespace ws
#endif