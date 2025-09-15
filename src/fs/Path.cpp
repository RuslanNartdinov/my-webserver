#include "webserv/fs/Path.hpp"
#include <sstream>

namespace ws
{

	static void split(const std::string &s, char sep, std::vector<std::string> &out)
	{
		out.clear();
		std::string cur;
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (s[i] == sep)
			{
				out.push_back(cur);
				cur.clear();
			}
			else
				cur.push_back(s[i]);
		}
		out.push_back(cur);
	}

	std::string pathJoin(const std::string &a, const std::string &b)
	{
		if (a.empty())
			return b;
		if (b.empty())
			return a;
		if (a[a.size() - 1] == '/')
			return a + b;
		return a + "/" + b;
	}

	std::string normalizePath(const std::string &p)
	{
		// работает в UNIX-стиле
		std::vector<std::string> parts, stack;
		split(p, '/', parts);
		for (size_t i = 0; i < parts.size(); ++i)
		{
			const std::string &s = parts[i];
			if (s.empty() || s == ".")
				continue;
			if (s == "..")
			{
				if (!stack.empty())
					stack.pop_back();
				continue;
			}
			stack.push_back(s);
		}
		std::string out = "/";
		for (size_t i = 0; i < stack.size(); ++i)
		{
			out += stack[i];
			if (i + 1 < stack.size())
				out += "/";
		}
		return out;
	}

	static bool hasPrefix(const std::string &a, const std::string &b)
	{
		// проверяет, что b начинается с a и следующим символом либо '/' либо конец
		if (b.size() < a.size())
			return false;
		if (b.compare(0, a.size(), a) != 0)
			return false;
		if (b.size() == a.size())
			return true;
		return b[a.size()] == '/';
	}

	bool startsWithPath(const std::string &base, const std::string &abs)
	{
		// входы предполагаются нормализованными и абсолютными
		return hasPrefix(base, abs);
	}
	static int hexVal(char c)
	{
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		return -1;
	}

	static std::string urlDecodeLenient(const std::string &s)
	{
		std::string out;
		out.reserve(s.size());
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (s[i] == '%' && i + 2 < s.size())
			{
				int hi = hexVal(s[i + 1]), lo = hexVal(s[i + 2]);
				if (hi >= 0 && lo >= 0)
				{
					out.push_back((char)((hi << 4) | lo));
					i += 2;
					continue;
				}
			}
			out.push_back(s[i]);
		}
		return out;
	}

	bool pathTraversalSuspect(const std::string &rawPath)
	{
		std::string dec = urlDecodeLenient(rawPath);
		if (dec.find("..") != std::string::npos)
			return true;

		// проверим сегменты
		size_t i = 0;
		while (i <= dec.size())
		{
			size_t j = dec.find('/', i);
			std::string seg = dec.substr(i, j == std::string::npos ? dec.size() - i : j - i);
			if (seg == "..")
				return true;
			if (j == std::string::npos)
				break;
			i = j + 1;
		}
		return false;
	}

} // namespace ws