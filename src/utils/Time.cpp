#include "webserv/utils/Time.hpp"
#include <ctime>
#include <string.h>

namespace ws {

static std::string fmtGmt(const std::tm& gmt) {
  char buf[64];
  std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
  return std::string(buf);
}

std::string httpDateNow() {
  std::time_t t = std::time(0);
  std::tm gmt;
#if defined(_WIN32)
  gmtime_s(&gmt, &t);
#else
  gmtime_r(&t, &gmt);
#endif
  return fmtGmt(gmt);
}

std::string httpDateFrom(time_t t) {
  std::tm gmt;
#if defined(_WIN32)
  gmtime_s(&gmt, &t);
#else
  gmtime_r(&t, &gmt);
#endif
  return fmtGmt(gmt);
}

time_t parseHttpDate(const std::string& s) {
  if (s.size() < 29) return (time_t)-1;
#if defined(_WIN32)
  return (time_t)-1;
#else
  std::tm tmv; memset(&tmv, 0, sizeof(tmv));
  char* res = strptime(s.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tmv);
  if (!res || *res != '\0') return (time_t)-1;
  return timegm(&tmv);
#endif
}

} // namespace ws