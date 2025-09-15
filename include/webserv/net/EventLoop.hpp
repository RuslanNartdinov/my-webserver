#ifndef WEBSERV_NET_EVENTLOOP_HPP
#define WEBSERV_NET_EVENTLOOP_HPP

#include <map>
#include <vector>
#include <string>
#include <utility>

#include "webserv/net/Poller.hpp"
#include "webserv/net/Listener.hpp"
#include "webserv/config/Config.hpp"
#include "webserv/http/Router.hpp"   // ← ДОБАВЛЕНО

namespace ws {

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    bool initFromConfig(const Config& cfg);
    int  run();

private:
    Poller _poller;
    std::vector<Listener*> _listeners;
    std::map<int, class Connection*> _conns;

    // ← ДОБАВЛЕНО: сопоставление fd слушателя -> (host,port)
    std::map<int, std::pair<std::string,int> > _listenerBind;

    // ← ДОБАВЛЕНО: роутер и ссылка на конфиг
    Router* _router;           // владеем
    const Config* _cfgRef;     // не владеем

    void rebuildPollSet();
    void acceptReady(int lfd);
    void gcClosed();
};

} // namespace ws
#endif