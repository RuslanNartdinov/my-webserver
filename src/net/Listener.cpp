#include "webserv/net/Listener.hpp"
#include "webserv/Log.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cstring>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <netdb.h> // ← добавь это
namespace ws
{
	bool setNonBlocking(int fd)
	{
		int fl = fcntl(fd, F_GETFL, 0);
		if (fl < 0)
			return false;
		return fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0;
	}
	bool setReuseAddr(int fd)
	{
		int yes = 1;
		return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == 0;
	}
	Listener::Listener() : _fd(-1) {}
	Listener::~Listener()
	{
		if (_fd >= 0)
			::close(_fd);
	}

	bool Listener::open(const std::string &host, int port)
	{
		_fd = ::socket(AF_INET, SOCK_STREAM, 0);
		if (_fd < 0)
		{
			ws::Log::error(std::string("socket() failed: ") + std::strerror(errno));
			return false;
		}
		if (!setReuseAddr(_fd))
			ws::Log::warn("setsockopt(SO_REUSEADDR) failed");
		if (!setNonBlocking(_fd))
		{
			ws::Log::error("fcntl(O_NONBLOCK) failed");
			return false;
		}

		struct sockaddr_in sa;
		std::memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_port = htons((uint16_t)port);

		// 1) прямой IPv4
		in_addr addr;
		addr.s_addr = inet_addr(host.c_str());

		if (addr.s_addr == INADDR_NONE)
		{
			// 2) спец-случай "0.0.0.0"
			if (host == "0.0.0.0")
			{
				sa.sin_addr.s_addr = INADDR_ANY;
			}
			else if (host == "localhost")
			{
				// 3) localhost -> 127.0.0.1
				sa.sin_addr.s_addr = inet_addr("127.0.0.1");
			}
			else
			{
				// 4) общий резолв DNS (только IPv4)
				struct addrinfo hints;
				std::memset(&hints, 0, sizeof(hints));
				hints.ai_family = AF_INET;
				hints.ai_socktype = SOCK_STREAM;
				hints.ai_flags = AI_ADDRCONFIG;

				struct addrinfo *res = 0;
				int rc = getaddrinfo(host.c_str(), NULL, &hints, &res);
				if (rc != 0)
				{
					ws::Log::error(std::string("getaddrinfo(\"") + host + "\") failed: " + gai_strerror(rc));
					return false;
				}
				// берём первый IPv4
				struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
				sa.sin_addr = sin->sin_addr;
				freeaddrinfo(res);
			}
		}
		else
		{
			sa.sin_addr = addr;
		}

		if (::bind(_fd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
		{
			ws::Log::error(std::string("bind() failed: ") + std::strerror(errno));
			return false;
		}
		if (::listen(_fd, 128) != 0)
		{
			ws::Log::error(std::string("listen() failed: ") + std::strerror(errno));
			return false;
		}

		_bind = host + ":" + (port <= 0 ? std::string("0") : std::to_string(port));
		ws::Log::info("Listening on " + _bind);
		return true;
	}
}