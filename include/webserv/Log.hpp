#ifndef WEBSERV_LOG_HPP
#define WEBSERV_LOG_HPP

#include <string>

namespace ws {

class Log {
public:
    enum Level { DEBUG, INFO, WARN, ERROR };

    static void write(Level lvl, const std::string &msg);
    static void info(const std::string &msg);
    static void warn(const std::string &msg);
    static void error(const std::string &msg);
    static void debug(const std::string &msg);

private:
    static const char* levelToStr(Level lvl);
};

}

#endif
