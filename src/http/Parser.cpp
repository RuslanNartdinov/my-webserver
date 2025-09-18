#include "webserv/http/Parser.hpp"
#include <sstream>
#include <cstdlib>
#include <cctype>

namespace ws
{

	static std::string trim(const std::string &s)
	{
		size_t a = 0, b = s.size();
		while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
			++a;
		while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
			--b;
		return s.substr(a, b - a);
	}

	static std::string toLower(const std::string &s)
	{
		std::string r(s);
		for (size_t i = 0; i < r.size(); ++i)
			r[i] = (char)std::tolower((unsigned char)r[i]);
		return r;
	}

	HttpParser::HttpParser()
		: maxRequestLine(8192),
		  maxHeaderBytes(65536),
		  maxBodyBytes(10 * 1024 * 1024),
		  _st(S_REQ_LINE),
		  /* _hdrEnd(0), */
		  _needBody(0)
	{
	}

	void HttpParser::feed(const char *data, size_t n)
	{
		_buf.append(data, n);
	}

	// --- ВАЖНО: сохраняем сырой request-target в _req.raw_target ---
	bool HttpParser::parseRequestLine(const std::string &line)
	{
		// METHOD SP REQUEST_TARGET SP HTTP/VERSION
		size_t a = 0, b = line.find(' ');
		if (b == std::string::npos)
			return false;
		_req.method = line.substr(a, b - a);

		a = b + 1;
		b = line.find(' ', a);
		if (b == std::string::npos)
			return false;

		// raw target как пришёл в строке запроса (до любой нормализации)
		std::string t = line.substr(a, b - a);
		_req.raw_target = t;

		// текущая логика сервера использует _req.target как «нормализованный»;
		// если нормализации нет — положим то же самое значение
		_req.target = t;

		a = b + 1;
		_req.version = line.substr(a);
		if (_req.version != "HTTP/1.1" && _req.version != "HTTP/1.0")
			return false;

		// допустимые методы
		if (_req.method != "GET" && _req.method != "POST" && _req.method != "DELETE" && _req.method != "HEAD" && _req.method != "PUT")
			return false;

		return true;
	}

	bool HttpParser::parseHeaders(const std::string &block)
	{
		size_t pos = 0;
		while (pos < block.size())
		{
			size_t end = block.find("\r\n", pos);
			if (end == std::string::npos)
				end = block.size();
			std::string line = block.substr(pos, end - pos);
			pos = end + 2;

			if (line.empty())
				continue;

			size_t colon = line.find(':');
			if (colon == std::string::npos)
				return false;

			std::string k = toLower(trim(line.substr(0, colon)));
			std::string v = trim(line.substr(colon + 1));
			_req.headers[k] = v;
		}
		return true;
	}

	HttpParser::Result HttpParser::parse(HttpRequest &out)
	{
		// 1) Request-Line
		if (_st == S_REQ_LINE)
		{
			size_t eol = _buf.find("\r\n");
			if (eol == std::string::npos)
			{
				if (_buf.size() > maxRequestLine)
					return BAD_REQUEST;
				return NEED_MORE;
			}
			std::string line = _buf.substr(0, eol);
			if (line.size() > maxRequestLine)
				return BAD_REQUEST;
			if (!parseRequestLine(line))
				return BAD_REQUEST;
			_buf.erase(0, eol + 2);
			_st = S_HEADERS;
		}

		// 2) Headers
		if (_st == S_HEADERS)
		{
			size_t endHeaders = _buf.find("\r\n\r\n");
			if (endHeaders == std::string::npos)
			{
				if (_buf.size() > maxHeaderBytes)
					return BAD_REQUEST;
				return NEED_MORE;
			}
			std::string headerBlock = _buf.substr(0, endHeaders);
			if (!parseHeaders(headerBlock))
				return BAD_REQUEST;
			_buf.erase(0, endHeaders + 4);

			// тело
			std::string te = _req.getHeader("transfer-encoding");
			std::string cl = _req.getHeader("content-length");

			if (!te.empty())
			{
				if (te != "chunked")
					return NOT_IMPLEMENTED; // другие TE не поддерживаем
				_st = S_BODY_CHUNKED;
			}
			else if (!cl.empty())
			{
				char *endp = 0;
				unsigned long v = std::strtoul(cl.c_str(), &endp, 10);
				if (endp == cl.c_str() || *endp != '\0')
					return BAD_REQUEST;
				_needBody = (size_t)v;
				if (_needBody > maxBodyBytes)
					return ENTITY_TOO_LARGE;
				if (_needBody == 0)
				{
					_st = S_DONE;
					out = _req;
					return OK;
				}
				_st = S_BODY_IDENTITY;
			}
			else
			{
				if (_req.method == "POST")
					return LENGTH_REQUIRED; // для POST нужен CL
				_st = S_DONE;
				out = _req;
				return OK; // GET/DELETE без тела
			}
		}

		// 3) BODY: Content-Length
		if (_st == S_BODY_IDENTITY)
		{
			if (_buf.size() < _needBody)
				return NEED_MORE;
			_req.body.assign(_buf.data(), _needBody);
			_buf.erase(0, _needBody);
			_st = S_DONE;
			out = _req;
			return OK;
		}

		// 3b) BODY: chunked
		if (_st == S_BODY_CHUNKED)
		{
			size_t consumed = 0;
			std::string outBody;
			bool done = _chunked.feed(_buf, consumed, outBody);
			_req.body.append(outBody);
			_buf.erase(0, consumed);

			if (!done)
				return NEED_MORE;
			if (_req.body.size() > maxBodyBytes)
				return ENTITY_TOO_LARGE;
			_st = S_DONE;
			out = _req;
			return OK;
		}

		// DONE
		if (_st == S_DONE)
		{
			out = _req;
			return OK;
		}

		return NEED_MORE;
	}
	void HttpParser::reset()
	{
		_buf.clear();
		_req = HttpRequest();
		_st = S_REQ_LINE;
		_needBody = 0;
		_chunked = ChunkedDecoder(); // если тип имеет дефолтный конструктор
	}

} // namespace ws