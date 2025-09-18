#include "webserv/net/ResponseBuilder.hpp"
#include "webserv/utils/Time.hpp"
#include <sstream>
#include <iomanip>

namespace ws {

static std::string hexLower(size_t x) {
  std::ostringstream oss;
  oss << std::hex << std::nouppercase << x;
  return oss.str();
}

std::string buildHeaders(int code,
                         const std::string& reason,
                         const std::string& ctype,
                         size_t clen,
                         bool keepAlive,
                         const std::string& location,
                         const std::string& dateStr,
                         const std::string& extra) {
  std::ostringstream oss;
  oss << "HTTP/1.1 " << code << ' ' << reason << "\r\n";
  oss << "Server: webserv-dev\r\n";
  oss << "Date: " << (dateStr.empty() ? httpDateNow() : dateStr) << "\r\n";
  oss << "Content-Type: " << ctype << "\r\n";
  oss << "Content-Length: " << (int)clen << "\r\n";
  oss << "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n";
  if (keepAlive) oss << "Keep-Alive: timeout=5, max=100\r\n";
  if (!location.empty()) oss << "Location: " << location << "\r\n";
  if (!extra.empty()) oss << extra;
  oss << "\r\n";
  return oss.str();
}

void appendChunked(std::string& out, const std::string& body) {
  out += hexLower(body.size());
  out += "\r\n";
  out += body;
  out += "\r\n0\r\n\r\n";
}

} // namespace ws