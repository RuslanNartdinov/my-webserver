#include "webserv/http/Request.hpp"
#include <algorithm>

namespace ws
{

	static std::string toLower(const std::string &s)
	{
		std::string r(s);
		for (size_t i = 0; i < r.size(); ++i)
			r[i] = (char)std::tolower((unsigned char)r[i]);
		return r;
	}

	bool HttpRequest::headerEquals(const std::string &name, const std::string &value) const
	{
		std::map<std::string, std::string>::const_iterator it = headers.find(toLower(name));
		return it != headers.end() && it->second == value;
	}

	bool HttpRequest::hasHeader(const std::string &name) const
	{
		return headers.find(toLower(name)) != headers.end();
	}

	std::string HttpRequest::getHeader(const std::string &name) const
	{
		std::map<std::string, std::string>::const_iterator it = headers.find(toLower(name));
		return it == headers.end() ? std::string() : it->second;
	}

} // namespace ws