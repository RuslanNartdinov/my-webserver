#ifndef WEBSERV_FS_PATH_HPP
#define WEBSERV_FS_PATH_HPP

#include <string>
#include <vector>

namespace ws
{

	std::string pathJoin(const std::string &a, const std::string &b);
	std::string normalizePath(const std::string &p);					  // убирает ".", "..", двойные "/"
	bool startsWithPath(const std::string &base, const std::string &abs); // abs лежит под base?
	bool pathTraversalSuspect(const std::string &rawPath);

} // namespace ws
#endif