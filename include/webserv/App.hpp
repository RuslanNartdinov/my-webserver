#include "webserv/config/Config.hpp"
namespace ws
{
	class App
	{
	public:
		App();
		int run(const std::string &configPath);

	private:
		bool fileExists(const std::string &path) const;
		ws::Config _cfg;
	};
}