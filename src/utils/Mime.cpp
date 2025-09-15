#include "webserv/utils/Mime.hpp"
#include <string>

namespace ws
{
	static bool ends(const std::string &s, const char *suf)
	{
		size_t ls = s.size(), lt = 0;
		while (suf[lt])
			++lt;
		return ls >= lt && s.compare(ls - lt, lt, suf) == 0;
	}
	std::string mimeByExt(const std::string &p)
	{
		if (ends(p, ".html") || ends(p, ".htm"))
			return "text/html; charset=utf-8";
		if (ends(p, ".css"))
			return "text/css; charset=utf-8";
		if (ends(p, ".js"))
			return "application/javascript; charset=utf-8";
		if (ends(p, ".json"))
			return "application/json; charset=utf-8";
		if (ends(p, ".png"))
			return "image/png";
		if (ends(p, ".jpg") || ends(p, ".jpeg"))
			return "image/jpeg";
		if (ends(p, ".gif"))
			return "image/gif";
		if (ends(p, ".svg"))
			return "image/svg+xml";
		if (ends(p, ".ico"))
			return "image/x-icon";
		if (ends(p, ".txt"))
			return "text/plain; charset=utf-8";
		return "application/octet-stream";
	}
}