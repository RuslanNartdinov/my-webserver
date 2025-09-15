#include "webserv/net/Poller.hpp"
#include <poll.h>

namespace ws
{

	Poller::Poller() {}
	Poller::~Poller() {}

	void Poller::clear() { _items.clear(); }

	void Poller::add(int fd, short events)
	{
		for (size_t i = 0; i < _items.size(); ++i)
			if (_items[i].fd == fd)
			{
				_items[i].events = events;
				return;
			}
		Item it;
		it.fd = fd;
		it.events = events; // <-- вместо Item{...}
		_items.push_back(it);
	}

	void Poller::mod(int fd, short events)
	{
		for (size_t i = 0; i < _items.size(); ++i)
			if (_items[i].fd == fd)
			{
				_items[i].events = events;
				return;
			}
		add(fd, events);
	}

	void Poller::del(int fd)
	{
		for (size_t i = 0; i < _items.size(); ++i)
			if (_items[i].fd == fd)
			{
				_items.erase(_items.begin() + i);
				return;
			}
	}

	int Poller::wait(std::vector<PollEvent> &out, int timeout_ms)
	{
		std::vector<struct pollfd> pfds(_items.size());
		for (size_t i = 0; i < _items.size(); ++i)
		{
			pfds[i].fd = _items[i].fd;
			pfds[i].events = _items[i].events;
			pfds[i].revents = 0;
		}
		int n = ::poll(pfds.data(), pfds.size(), timeout_ms);
		if (n <= 0)
			return n;

		out.clear();
		out.reserve(pfds.size());
		for (size_t i = 0; i < pfds.size(); ++i)
		{
			if (pfds[i].revents)
			{
				PollEvent ev; // <-- вместо PollEvent{...}
				ev.fd = pfds[i].fd;
				ev.events = pfds[i].events;
				ev.revents = pfds[i].revents;
				out.push_back(ev);
			}
		}
		return static_cast<int>(out.size());
	}
}