#ifndef WEBSERV_HTTP_PARSER_HPP
#define WEBSERV_HTTP_PARSER_HPP

#include "webserv/http/Request.hpp"
#include "webserv/http/Chunked.hpp"
#include <string>

namespace ws
{

	class HttpParser
	{
	public:
		HttpParser();

		enum Result
		{
			NEED_MORE,
			OK,
			BAD_REQUEST,
			NOT_IMPLEMENTED,
			LENGTH_REQUIRED,
			ENTITY_TOO_LARGE
		};

		// Подать новые данные (неблокирующая модель): buf добавляется к внутреннему накопителю.
		void feed(const char *data, size_t n);

		// Пытается распарсить запрос. При OK — HttpRequest заполнен и тело декодировано.
		Result parse(HttpRequest &out);

		// Настройки/лимиты:
		size_t maxRequestLine; // 8 KB
		size_t maxHeaderBytes; // 64 KB
		size_t maxBodyBytes;   // 10 MB (или меньше из конфига позже)
		void reset();

	private:
		enum State
		{
			S_REQ_LINE,
			S_HEADERS,
			S_BODY_IDENTITY,
			S_BODY_CHUNKED,
			S_DONE
		} _st;
		std::string _buf; // входящий буфер (сырые байты)
		//size_t _hdrEnd;	  // позиция конца заголовков (\r\n\r\n)
		size_t _needBody; // для Content-Length
		ChunkedDecoder _chunked;
		HttpRequest _req;

		bool parseRequestLine(const std::string &line);
		bool parseHeaders(const std::string &block);
	};

} // namespace ws
#endif