#ifndef WEBSERV_NET_LISTENER_HPP
#define WEBSERV_NET_LISTENER_HPP

#include <string>

namespace ws {

class Listener {
public:
    Listener();
    ~Listener();

    bool open(const std::string& host, int port);
    int  fd()    const { return _fd; }
    std::string bindStr() const { return _bind; }

private:
    int         _fd;
    std::string _bind;

    // запрет копирования (C++98-совместимо: объявлены, без реализации)
    Listener(const Listener&);
    Listener& operator=(const Listener&);
};

// вспомогательные функции (реализованы в Listener.cpp)
bool setNonBlocking(int fd);
bool setReuseAddr(int fd);

} // namespace ws

#endif // WEBSERV_NET_LISTENER_HPP