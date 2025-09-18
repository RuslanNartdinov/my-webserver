#include "webserv/net/MethodGate.hpp"
#include <algorithm>

namespace ws {

bool isImplemented(const std::string& m) {
  return m == "GET" || m == "POST" || m == "DELETE" || m == "HEAD";
}

static bool contains(const ws::Location* loc, const std::string& m) {
  if (!loc) return false;
  for (size_t i = 0; i < loc->allow_methods.size(); ++i)
    if (loc->allow_methods[i] == m) return true;
  return false;
}

bool isAllowed(const ws::Location* loc, const std::string& method) {
  if (!loc) return (method=="GET" || method=="POST" || method=="DELETE");
  if (method == "HEAD") {
    return contains(loc, "GET");
  }
  return contains(loc, method);
}

std::string buildAllowHeader(const ws::Location* loc) {
  std::string allow;
  if (loc) {
    bool hasGet=false, hasPost=false, hasDel=false;
    for (size_t i=0;i<loc->allow_methods.size();++i) {
      if (loc->allow_methods[i]=="GET")  hasGet=true;
      if (loc->allow_methods[i]=="POST") hasPost=true;
      if (loc->allow_methods[i]=="DELETE") hasDel=true;
    }
    if (hasGet)  { if (!allow.empty()) allow+=", "; allow+="GET"; }
    if (hasPost) { if (!allow.empty()) allow+=", "; allow+="POST"; }
    if (hasDel)  { if (!allow.empty()) allow+=", "; allow+="DELETE"; }
    if (hasGet)  { if (!allow.empty()) allow+=", "; allow+="HEAD"; }
  } else {
    allow = "GET, POST, DELETE, HEAD";
  }
  return allow;
}

} // namespace ws