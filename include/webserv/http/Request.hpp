#ifndef WEBSERV_HTTP_REQUEST_HPP
#define WEBSERV_HTTP_REQUEST_HPP

#include <string>
#include <map>

namespace ws {

struct HttpRequest {
    // request line
    std::string method;
    std::string target;     // normalized/used by router
    std::string version;

    // headers/body
    std::map<std::string,std::string> headers; // lower-case keys
    std::string body;                          // de-chunked if chunked

    // raw target from the request line (pre-normalization)
    std::string raw_target;

    // helpers
    bool headerEquals(const std::string &name, const std::string &value) const;
    bool hasHeader(const std::string &name) const;
    std::string getHeader(const std::string &name) const;

    const std::string &getRawTarget() const { return raw_target; }
};

} // namespace ws
#endif