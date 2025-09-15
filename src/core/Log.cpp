#include "webserv/Log.hpp"
#include <iostream>
#include <ctime>

namespace ws {

const char* Log::levelToStr(Log::Level lvl) {
    switch (lvl) {
        case DEBUG: return "DEBUG";
        case INFO:  return "INFO ";
        case WARN:  return "WARN ";
        case ERROR: return "ERROR";
    }
    return "INFO ";
}

static std::string nowIso8601() {
    std::time_t t = std::time(0);
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return std::string(buf);
}

void Log::write(Level lvl, const std::string &msg) {
    std::cerr << "[" << nowIso8601() << "] "
              << levelToStr(lvl) << " "
              << msg << std::endl;
}

void Log::info(const std::string &msg)  { write(INFO,  msg); }
void Log::warn(const std::string &msg)  { write(WARN,  msg); }
void Log::error(const std::string &msg) { write(ERROR, msg); }
void Log::debug(const std::string &msg) { write(DEBUG, msg); }

}
