#ifndef WEBSERV_NET_LISTENER_HPP
#define WEBSERV_NET_LISTENER_HPP
#include <string>
namespace ws
{
	class Listener
	{
	public:
		Listener();
		~Listener();
		bool open(const std::string &host, int port);
		int fd() const { return _fd; }
		std::string bindStr() const { return _bind; }

	private:
		int _fd;
		std::string _bind;
	};
	bool setNonBlocking(int fd);
	bool setReuseAddr(int fd);
}
#endif