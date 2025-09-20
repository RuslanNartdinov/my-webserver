#include "webserv/net/EventLoop.hpp"
#include "webserv/net/Connection.hpp"
#include "webserv/net/Listener.hpp"
#include "webserv/net/Poller.hpp"
#include "webserv/http/Router.hpp"
#include "webserv/Log.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

#include <map>
#include <vector>
#include <utility>
#include <sstream>

namespace ws {

EventLoop::EventLoop()
    : _router(0), _cfgRef(0) {}

EventLoop::~EventLoop() {
    // закрыть и удалить слушатели
    for (size_t i = 0; i < _listeners.size(); ++i) {
        delete _listeners[i];
    }
    _listeners.clear();
    _listenerBind.clear();

    // удалить активные соединения (на всякий случай)
    for (std::map<int, Connection*>::iterator it = _conns.begin(); it != _conns.end(); ++it) {
        delete it->second;
    }
    _conns.clear();

    if (_router) { delete _router; _router = 0; }
}

bool EventLoop::initFromConfig(const Config& cfg) {
    _cfgRef = &cfg;

    // переcобрать роутер
    if (_router) { delete _router; _router = 0; }
    _router = new Router(_cfgRef);

    // подчистить прежние слушатели/бинды
    for (size_t i = 0; i < _listeners.size(); ++i) delete _listeners[i];
    _listeners.clear();
    _listenerBind.clear();

    // собрать уникальные (host,port)
    std::vector< std::pair<std::string,int> > binds;
    for (size_t i = 0; i < cfg.servers.size(); ++i) {
        const ServerConfig& s = cfg.servers[i];
        bool exists = false;
        for (size_t j = 0; j < binds.size(); ++j) {
            if (binds[j].first == s.host && binds[j].second == s.port) { exists = true; break; }
        }
        if (!exists) binds.push_back(std::make_pair(s.host, s.port));
    }

    // создать слушатель на каждый уникальный бинд
    for (size_t i = 0; i < binds.size(); ++i) {
        const std::string& host = binds[i].first;
        int port = binds[i].second;

        Listener* L = new Listener();
        if (!L->open(host, port)) {
            std::ostringstream oss;
            oss << "Listener open failed for " << host << ":" << port;
            ws::Log::warn(oss.str());
            delete L;
            return false; // “всё или ничего”
        }
        _listeners.push_back(L);
        _listenerBind[L->fd()] = std::make_pair(host, port);
    }

    return true;
}

void EventLoop::rebuildPollSet() {
    _poller.clear();

    // слушатели
    for (size_t i = 0; i < _listeners.size(); ++i) {
        _poller.add(_listeners[i]->fd(), POLLIN);
    }

    // активные соединения
    for (std::map<int, Connection*>::iterator it = _conns.begin(); it != _conns.end(); ++it) {
        _poller.add(it->first, it->second->wantEvents());
    }
}

void EventLoop::acceptReady(int lfd) {
    for (;;) {
        int cfd = ::accept(lfd, 0, 0);
        if (cfd < 0) {
            // не трогаем errno (по твоему требованию) — просто ждём следующего POLLIN
            return;
        }
        setNonBlocking(cfd);

        Connection* c = new Connection(cfd);

        // передадим, на каком (host,port) нас приняли
        std::map<int, std::pair<std::string,int> >::iterator itB = _listenerBind.find(lfd);
        if (itB != _listenerBind.end()) {
            c->setLocalBind(itB->second.first, itB->second.second);
        }

        c->setRouter(_router);
        _conns[cfd] = c;
    }
}

void EventLoop::gcClosed() {
    std::vector<int> dead;
    for (std::map<int, Connection*>::iterator it = _conns.begin(); it != _conns.end(); ++it) {
        if (it->second->isClosed()) dead.push_back(it->first);
    }
    for (size_t i = 0; i < dead.size(); ++i) {
        int fd = dead[i];
        std::map<int, Connection*>::iterator it = _conns.find(fd);
        if (it != _conns.end()) {
            delete it->second;
            _conns.erase(it);
        }
    }
}

int EventLoop::run() {
    ws::Log::info("Event loop started");

    std::vector<PollEvent> evs;

    while (true) {
        rebuildPollSet();

        int n = _poller.wait(evs, 1000);
        if (n < 0) continue;

        for (size_t i = 0; i < evs.size(); ++i) {
            int fd   = evs[i].fd;
            short ev = evs[i].revents;

            // это слушатель?
            bool isL = false;
            for (size_t k = 0; k < _listeners.size(); ++k) {
                if (_listeners[k]->fd() == fd) { isL = true; break; }
            }

            if (isL) {
                if (ev & POLLIN) acceptReady(fd);
                continue;
            }

            // иначе — соединение
            std::map<int, Connection*>::iterator it = _conns.find(fd);
            if (it == _conns.end()) continue;

            if (ev & (POLLERR | POLLHUP | POLLNVAL)) {
                if (it->second->wantEvents() & POLLOUT) it->second->onWritable();
                else if (it->second->wantEvents() & POLLIN) it->second->onReadable();
                continue;
            }

            if (ev & POLLIN)  it->second->onReadable();
            if (ev & POLLOUT) it->second->onWritable();
        }

        gcClosed();
    }

    return 0;
}

} // namespace ws