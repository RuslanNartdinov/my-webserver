#ifndef WEBSERV_NET_POLLER_HPP
#define WEBSERV_NET_POLLER_HPP

#include <vector>

namespace ws {

struct PollEvent {
    int fd;
    short events;   // интересующие события
    short revents;  // готовые события от poll()
};

class Poller {
public:
    Poller();        // объявляем явно
    ~Poller();       // объявляем явно

    void clear();
    void add(int fd, short events);
    void mod(int fd, short events);
    void del(int fd);
    int  wait(std::vector<PollEvent>& out, int timeout_ms);

private:
    struct Item { int fd; short events; };
    std::vector<Item> _items;
};

} // namespace ws
#endif