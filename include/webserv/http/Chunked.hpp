#ifndef WEBSERV_HTTP_CHUNKED_HPP
#define WEBSERV_HTTP_CHUNKED_HPP

#include <string>

namespace ws
{

	// Простое состояние для TE: chunked
	class ChunkedDecoder
	{
	public:
		ChunkedDecoder();
		// скармливаем сырые байты; по мере декодирования дописываем в out
		// возвращает true, если весь поток (включая 0-chunk и CRLF) дочитан
		bool feed(const std::string &in, size_t &consumed, std::string &out);

	private:
		enum State
		{
			S_SIZE,
			S_DATA,
			S_CRLF_AFTER_DATA,
			S_CRLF_AFTER_SIZE,
			S_DONE,
			S_BAD
		} _st;
		size_t _need; // сколько данных нужно дочитать для текущего чанка

		// парсинг hex размера из начала строки (до ; или CRLF)
		bool parseChunkSizeLine(const std::string &in, size_t &off, size_t &size);
	};

} // namespace ws
#endif