#include "webserv/http/Chunked.hpp"
#include <cctype>
#include <cstdlib>

namespace ws
{

	ChunkedDecoder::ChunkedDecoder() : _st(S_SIZE), _need(0) {}

	bool ChunkedDecoder::parseChunkSizeLine(const std::string &in, size_t &off, size_t &size)
	{
		// читаем до CRLF; допускаем до ';' (chunk-ext) игнорируем
		size_t i = off;
		size_t len = in.size();
		// найдём CRLF
		size_t lineEnd = std::string::npos;
		for (; i + 1 < len; ++i)
			if (in[i] == '\r' && in[i + 1] == '\n')
			{
				lineEnd = i;
				break;
			}
		if (lineEnd == std::string::npos)
			return false; // не хватает данных

		// parse hex
		size_t j = off;
		size = 0;
		for (; j < lineEnd && in[j] != ';'; ++j)
		{
			char c = in[j];
			int v = -1;
			if (c >= '0' && c <= '9')
				v = c - '0';
			else if (c >= 'a' && c <= 'f')
				v = 10 + (c - 'a');
			else if (c >= 'A' && c <= 'F')
				v = 10 + (c - 'A');
			else
				return false;
			size = (size << 4) + (size_t)v;
		}
		off = lineEnd + 2; // после CRLF
		return true;
	}

	bool ChunkedDecoder::feed(const std::string &in, size_t &consumed, std::string &out)
	{
		size_t off = consumed;
		while (off <= in.size())
		{
			switch (_st)
			{
			case S_SIZE:
			{
				size_t sz = 0;
				if (!parseChunkSizeLine(in, off, sz))
				{
					consumed = off;
					return false;
				} // ждём ещё
				_need = sz;
				if (_need == 0)
				{
					_st = S_CRLF_AFTER_SIZE;
				}
				else
				{
					_st = S_DATA;
				}
			}
			break;
			case S_DATA:
			{
				size_t left = in.size() - off;
				if (left == 0)
				{
					consumed = off;
					return false;
				}
				size_t take = _need < left ? _need : left;
				out.append(in.data() + off, take);
				off += take;
				_need -= take;
				if (_need == 0)
					_st = S_CRLF_AFTER_DATA;
			}
			break;
			case S_CRLF_AFTER_DATA:
			{
				if (in.size() - off < 2)
				{
					consumed = off;
					return false;
				}
				if (!(in[off] == '\r' && in[off + 1] == '\n'))
				{
					_st = S_BAD;
					consumed = off;
					return true;
				}
				off += 2;
				_st = S_SIZE;
			}
			break;
			case S_CRLF_AFTER_SIZE:
			{ // после 0-size обязателен финальный CRLF
				if (in.size() - off < 2)
				{
					consumed = off;
					return false;
				}
				if (!(in[off] == '\r' && in[off + 1] == '\n'))
				{
					_st = S_BAD;
					consumed = off;
					return true;
				}
				off += 2;
				_st = S_DONE;
				consumed = off;
				return true;
			}
			case S_DONE:
				consumed = off;
				return true;
			case S_BAD:
				consumed = off;
				return true;
			}
		}
		consumed = off;
		return false;
	}

} // namespace ws