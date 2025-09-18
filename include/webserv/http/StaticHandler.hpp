#pragma once

#include <string>
#include <vector>

namespace ws
{
	// forward — но мы будем включать config/Request.hpp в .cpp
	struct ServerConfig;
	struct Location;
	struct HttpRequest;

	struct StaticResult
	{
		int status;
		std::string reason;
		std::string body;
		std::string contentType;
		size_t contentLength;
		std::string location;       // для редиректа 3xx
		std::string extraHeaders;   // "ETag", "Last-Modified", "Allow" и т.п.

		StaticResult()
			: status(200),
			  contentLength(0)
		{
		}
	};

	class StaticHandler
	{
	public:
		static bool handleGET(const ServerConfig &srv,
							  const Location *loc,
							  const HttpRequest &req,
							  StaticResult &out);

		// helpers
		static std::string pathOnly(const std::string &target);
		static bool isDir(const std::string &p);
		static bool isFile(const std::string &p);
		static bool readFile(const std::string &fsPath, std::string &out);
		static std::string dirListingHtml(const std::string &reqPath, const std::string &fsDir);

		static std::string mapToFsPath(const ServerConfig &srv,
									   const Location *loc,
									   const std::string &reqPath,
									   bool &ok);
	};
}