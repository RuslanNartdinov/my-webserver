#include "webserv/net/EventLoop.hpp"
#include "webserv/net/Connection.hpp"
#include "webserv/Log.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <errno.h>
#include <poll.h>

namespace ws
{

	EventLoop::EventLoop() {}
	EventLoop::~EventLoop()
	{
		for (size_t i = 0; i < _listeners.size(); ++i)
			delete _listeners[i];
		for (std::map<int, Connection *>::iterator it = _conns.begin(); it != _conns.end(); ++it)
			delete it->second;
		delete _router; // ← ДОБАВЛЕНО
	}

	bool EventLoop::initFromConfig(const Config &cfg)
	{
		_cfgRef = &cfg; // ← ДОБАВЛЕНО
		if (_router)
		{
			delete _router;
		} // на случай повторной инициализации
		_router = new Router(_cfgRef); // ← ДОБАВЛЕНО

		std::vector<std::pair<std::string, int> > binds;
		for (size_t i = 0; i < cfg.servers.size(); ++i)
		{
			const ServerConfig &s = cfg.servers[i];
			bool exists = false;
			for (size_t j = 0; j < binds.size(); ++j)
				if (binds[j].first == s.host && binds[j].second == s.port)
				{
					exists = true;
					break;
				}
			if (!exists)
				binds.push_back(std::make_pair(s.host, s.port));
		}

		for (size_t i = 0; i < binds.size(); ++i)
		{
			Listener *L = new Listener();
			if (!L->open(binds[i].first, binds[i].second))
			{
				delete L;
				return false;
			}
			_listeners.push_back(L);
			_listenerBind[L->fd()] = std::make_pair(binds[i].first, binds[i].second); // ← ДОБАВЛЕНО
		}
		return true;
	}
	void EventLoop::rebuildPollSet()
	{
		_poller.clear();
		for (size_t i = 0; i < _listeners.size(); ++i)
			_poller.add(_listeners[i]->fd(), POLLIN);
		for (std::map<int, Connection *>::iterator it = _conns.begin(); it != _conns.end(); ++it)
			_poller.add(it->first, it->second->wantEvents());
	}
	void EventLoop::acceptReady(int lfd)
	{
		for (;;)
		{
			int cfd = ::accept(lfd, 0, 0);
			if (cfd < 0)
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					return;
				ws::Log::warn("accept() error");
				return;
			}
			setNonBlocking(cfd);
			Connection *c = new Connection(cfd); // ← ВАЖНО: именуем как c
			// проставим локальный bind из слушателя
			std::map<int, std::pair<std::string, int> >::iterator itB = _listenerBind.find(lfd);
			if (itB != _listenerBind.end())
			{
				c->setLocalBind(itB->second.first, itB->second.second); // ← ИСПОЛЬЗУЕМ c
			}
			// передадим роутер
			c->setRouter(_router); // ← ИСПОЛЬЗУЕМ c
			_conns[cfd] = c;
		}
	}
	void EventLoop::gcClosed()
	{
		std::vector<int> dead;
		for (std::map<int, Connection *>::iterator it = _conns.begin(); it != _conns.end(); ++it)
			if (it->second->isClosed())
				dead.push_back(it->first);
		for (size_t i = 0; i < dead.size(); ++i)
		{
			delete _conns[dead[i]];
			_conns.erase(dead[i]);
		}
	}
	int EventLoop::run()
	{
		ws::Log::info("Event loop started");
		std::vector<PollEvent> evs;
		while (true)
		{
			rebuildPollSet();
			int n = _poller.wait(evs, 1000);
			if (n < 0)
				continue;
			for (size_t i = 0; i < evs.size(); ++i)
			{
				int fd = evs[i].fd;
				short rev = evs[i].revents;
				bool isL = false;
				for (size_t k = 0; k < _listeners.size(); ++k)
					if (_listeners[k]->fd() == fd)
					{
						isL = true;
						break;
					}
				if (isL)
				{
					if (rev & POLLIN)
						acceptReady(fd);
					continue;
				}
				std::map<int, Connection *>::iterator it = _conns.find(fd);
				if (it == _conns.end())
					continue;
				if (rev & (POLLERR | POLLHUP | POLLNVAL))
				{
					// Если есть что писать — попробуем выслать перед закрытием
					if (it->second->wantEvents() & POLLOUT)
					{
						it->second->onWritable();
					}
					else if (it->second->wantEvents() & POLLIN)
					{
						it->second->onReadable();
					}
					continue;
				}
				if (rev & POLLIN)
					it->second->onReadable();
				if (rev & POLLOUT)
					it->second->onWritable();
			}
			gcClosed();
		}
		return 0;
	}
}