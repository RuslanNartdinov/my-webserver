#ifndef WEBSERV_CONFIG_HPP
#define WEBSERV_CONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <stdexcept>

namespace ws {

struct Location {
    std::string path;
    std::vector<std::string> allow_methods;
    std::string root;
    std::string alias;
    std::vector<std::string> index;
    bool autoindex;
    bool upload_enable;
    std::string upload_store;
    int return_code;
    std::string return_url;
    std::string cgi_ext;
    std::string cgi_bin;
    size_t client_max_body_size;

    Location() : autoindex(false), upload_enable(false),
                 return_code(0), client_max_body_size(0) {}
};

struct ServerConfig {
    std::string host;
    int         port;
    std::vector<std::string> server_names;
    std::string root;
    std::map<int, std::string> error_pages;
    size_t client_max_body_size;
    std::vector<Location> locations;

    ServerConfig() : port(80), client_max_body_size(1<<20) {}
};

struct Config {
    std::vector<ServerConfig> servers;
};

struct ConfigError : public std::runtime_error {
    size_t line, col;
    ConfigError(const std::string& msg, size_t ln, size_t cl)
        : std::runtime_error(msg), line(ln), col(cl) {}
};

size_t parseSizeWithUnits(const std::string& s, size_t ln, size_t col);

} // namespace ws
#endif