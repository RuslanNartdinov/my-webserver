#ifndef WEBSERV_HTTP_ROUTER_HPP
#define WEBSERV_HTTP_ROUTER_HPP

#include "webserv/config/Config.hpp"
#include <string>

namespace ws
{

	struct RouteMatch
	{
		const ServerConfig *server;
		const Location *location; // может быть NULL, если не найдено
	};

	class Router
	{
	public:
		Router(const Config *cfg);

		// listenerHost/Port — куда пришёл сокет (из Listener),
		// hostHeader — значение Host из запроса (может быть пустым)
		RouteMatch resolve(const std::string &listenerHost, int listenerPort,
						   const std::string &hostHeader,
						   const std::string &requestTarget) const;

	private:
		const Config *_cfg;

		const ServerConfig *pickServer(const std::string &lhost, int lport,
									   const std::string &hostHeader) const;
		const Location *pickLocation(const ServerConfig *srv,
									 const std::string &path) const;
	};
	inline bool methodAllowed(const Location *loc, const std::string &m)
	{
		if (!loc || loc->allow_methods.empty())
			return true; // по умолчанию всё три
		for (size_t i = 0; i < loc->allow_methods.size(); ++i)
			if (loc->allow_methods[i] == m)
				return true;
		return false;
	}

} // namespace ws
#endif