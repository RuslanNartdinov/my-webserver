#ifndef WEBSERV_NET_CONNECTION_HPP
#define WEBSERV_NET_CONNECTION_HPP
#include <string>
#include "webserv/http/Parser.hpp"
#include "webserv/http/Request.hpp"
#include "webserv/http/Router.hpp"
namespace ws
{
	class Connection
	{
	public:
		enum State
		{
			READ,
			PROCESS,
			WRITE,
			CLOSED
		};
		Connection(int fd) : _fd(fd), _state(READ), _curKeepAlive(false), _reqsOnConn(0) {}
		~Connection();
		int fd() const { return _fd; }
		short wantEvents() const;
		void onReadable();
		void onWritable();
		bool isClosed() const { return _state == CLOSED; }
		bool handlePostUpload(const RouteMatch& m);
		void setRouter(const Router *r) { _router = r; }
		void setLocalBind(const std::string &host, int port)
		{
			_lhost = host;
			_lport = port;
		}

	private:
		void makeChunkedResponse(int code, const std::string &reason,
								 const std::string &ctype,
								 const std::string &body,
								 const std::string &extra = "");
		int _fd;
		State _state;
		std::string _in, _out;
		const Router *_router;

		std::string _lhost;
		int _lport; // ← сюда EventLoop проставит host:port слушателя
		HttpParser _parser;
		HttpRequest _req;
		bool _curKeepAlive; // текущий ответ: держим соединение?
		int _reqsOnConn;	// сколько запросов обработали в этом TCP
		static const int MAX_KEEPALIVE = 100;

		bool shouldKeepAlive(const HttpRequest &r) const;
		void makeErrorWithPages(int code, const ServerConfig *srv);
		void makeResponse(int code, const std::string &reason,
						  const std::string &ctype,
						  const std::string &body,
						  const std::string &location = std::string());
		void makeResponseHeaders(int code, const std::string &reason,
								 const std::string &ctype, size_t clen,
								 const std::string &location,
								 const std::string &extra);

		void closeNow();
		void makeError(int code, const std::string &text);
		void makeOkText(const std::string &text);
	};
}
#endif