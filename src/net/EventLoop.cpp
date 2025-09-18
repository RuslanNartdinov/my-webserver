#include "webserv/net/EventLoop.hpp"
#include "webserv/net/Connection.hpp"
#include "webserv/net/Listener.hpp"
#include "webserv/net/Poller.hpp"
#include "webserv/http/Router.hpp"
#include "webserv/Log.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <utility> // std::pair, std::make_pair
#include <map>
#include <vector>

namespace ws
{

	EventLoop::EventLoop()
		: _router(0), _cfgRef(0)
	{
	}

	EventLoop::~EventLoop()
	{
		// НИЧЕГО не удаляем вручную:
		// - _listeners хранит Listener по значению
		// - _conns мы чистим в gcClosed()
		if (_router)
		{
			delete _router;
			_router = 0;
		}
	}

	bool EventLoop::initFromConfig(const Config &cfg)
	{
		_cfgRef = &cfg;

		// Роутер пересобираем заново
		if (_router)
		{
			delete _router;
			_router = 0;
		}
		_router = new Router(_cfgRef);

		// очистим предыдущие слушатели и их бинды
		_listeners.clear();
		_listenerBind.clear();

		// Список уникальных (host, port) биндов
		std::vector<std::pair<std::string, int> > binds;
		for (size_t i = 0; i < cfg.servers.size(); ++i)
		{
			const ServerConfig &s = cfg.servers[i];
			bool exists = false;
			for (size_t j = 0; j < binds.size(); ++j)
			{
				if (binds[j].first == s.host && binds[j].second == s.port)
				{
					exists = true;
					break;
				}
			}
			if (!exists)
				binds.push_back(std::make_pair(s.host, s.port));
		}

		// Создаём Listener по значению и открываем
		for (size_t i = 0; i < binds.size(); ++i)
		{
			const std::string &host = binds[i].first;
			int port = binds[i].second;

			// Кладём объект по значению (без new/delete)
			_listeners.push_back(Listener());
			Listener &L = _listeners.back();

			if (!L.open(host, port))
			{
				// откатим добавление и сообщим об ошибке
				_listeners.pop_back();
				return false;
			}

			// Доп. карта: fd -> (host,port), пригодится для Connection
			_listenerBind[L.fd()] = std::make_pair(host, port);
		}

		return true;
	}

	void EventLoop::rebuildPollSet()
	{
		_poller.clear();

		// слушатели
		for (size_t i = 0; i < _listeners.size(); ++i)
			_poller.add(_listeners[i].fd(), POLLIN);

		// активные соединения
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
				// не трогаем errno — выходим, дождёмся следующего POLLIN на слушателе
				return;
			}
			setNonBlocking(cfd);
			Connection *c = new Connection(cfd);
			std::map<int, std::pair<std::string, int> >::iterator itB = _listenerBind.find(lfd);
			if (itB != _listenerBind.end())
				c->setLocalBind(itB->second.first, itB->second.second);
			c->setRouter(_router);
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
			int fd = dead[i];
			std::map<int, Connection *>::iterator it = _conns.find(fd);
			if (it != _conns.end())
			{
				delete it->second;
				_conns.erase(it);
			}
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

				// это Listener?
				bool isL = false;
				for (size_t k = 0; k < _listeners.size(); ++k)
				{
					if (_listeners[k].fd() == fd)
					{
						isL = true;
						break;
					}
				}

				if (isL)
				{
					if (rev & POLLIN)
						acceptReady(fd);
					continue;
				}

				// иначе это соединение
				std::map<int, Connection *>::iterator it = _conns.find(fd);
				if (it == _conns.end())
					continue;

				if (rev & (POLLERR | POLLHUP | POLLNVAL))
				{
					// Попробуем дописать, если есть что отправлять
					if (it->second->wantEvents() & POLLOUT)
						it->second->onWritable();
					else if (it->second->wantEvents() & POLLIN)
						it->second->onReadable();
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

} // namespace ws