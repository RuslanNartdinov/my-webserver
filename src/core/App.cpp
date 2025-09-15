#include "webserv/App.hpp"
#include "webserv/Log.hpp"
#include "webserv/Version.hpp"

#include "webserv/config/Lexer.hpp"
#include "webserv/config/Parser.hpp"
#include "webserv/net/EventLoop.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace ws
{

	App::App() {}

	bool App::fileExists(const std::string &path) const
	{
		std::ifstream f(path.c_str());
		return f.good();
	}

	int App::run(const std::string &configPath)
	{
		ws::Log::info(std::string(WEBSERV_NAME) + " " + WEBSERV_VERSION + " starting…");

		if (!fileExists(configPath))
		{
			ws::Log::error("Config not found: " + configPath);
			return 1;
		}
		ws::Log::info("Using config: " + configPath);

		std::ifstream ifs(configPath.c_str());
		std::stringstream buf;
		buf << ifs.rdbuf();
		std::string text = buf.str();

		try
		{
			ws::Lexer lx(text);
			ws::Parser p(lx);
			_cfg = p.parse();
			ws::Log::info("Parsed servers: " + std::string(_cfg.servers.empty() ? "0" : "OK"));
			ws::EventLoop loop;
			if (!loop.initFromConfig(_cfg))
			{
				ws::Log::error("Network init failed");
				return 3;
			}
			return loop.run();
		}
		catch (const ws::ConfigError &e)
		{
			std::ostringstream oss;
			oss << "Config error at " << e.line << ":" << e.col << " - " << e.what();
			ws::Log::error(oss.str());
			return 2;
		}

		ws::Log::info("Stage 1 OK (config parsed). Exiting.");
		return 0;
	}

} // namespace ws