#include "webserv/net/DeleteHandler.hpp"
#include "webserv/fs/Path.hpp"
#include <sys/stat.h>
#include <unistd.h>

namespace ws {

static bool isFile(const std::string& p) {
  struct stat st; if (stat(p.c_str(), &st)!=0) return false;
  return S_ISREG(st.st_mode);
}

static std::string pathOnly(const std::string& target) {
  size_t q = target.find('?');
  return q == std::string::npos ? target : target.substr(0, q);
}

static std::string mapFs(const ws::ServerConfig* srv, const ws::Location* loc,
                         const std::string& reqPathIn, bool& ok) {
  ok = true; if (!srv) { ok=false; return std::string(); }
  std::string reqPath = reqPathIn;
  std::string base; std::string rest = reqPath;

  if (loc && !loc->alias.empty()) {
    std::string lpath = loc->path;
    if (!lpath.empty() && lpath[0] != '/') lpath = "/" + lpath;
    if (reqPath.size() >= lpath.size() && reqPath.compare(0, lpath.size(), lpath) == 0) {
      std::string tail = reqPath.substr(lpath.size());
      if (!tail.empty() && tail[0] == '/') tail.erase(0,1);
      base = loc->alias; rest = tail;
    } else {
      base = srv->root.empty() ? "." : srv->root;
      if (!rest.empty() && rest[0] == '/') rest.erase(0,1);
    }
  } else {
    base = (loc && !loc->root.empty()) ? loc->root : (srv->root.empty() ? "." : srv->root);
    if (!rest.empty() && rest[0] == '/') rest.erase(0,1);
  }

  std::string joined = ws::pathJoin(base, rest);
  std::string norm = ws::normalizePath("/" + joined);
  std::string normBase = ws::normalizePath("/" + base);

  if (norm.size() < normBase.size() ||
      norm.compare(0, normBase.size(), normBase) != 0 ||
      (norm.size() > normBase.size() && norm[normBase.size()] != '/')) {
    ok = false; return std::string();
  }

  if (!norm.empty() && norm[0] == '/') return norm.substr(1);
  return norm;
}

int handleDelete(const ws::RouteMatch& m, const ws::HttpRequest& req) {
  if (req.method != "DELETE") return 0;

  const std::string raw = req.getRawTarget().empty() ? req.target : req.getRawTarget();
  std::string p = pathOnly(raw);
  if (ws::pathTraversalSuspect(p)) return 403;

  bool okMap = false;
  std::string fs = mapFs(m.server, m.location, p, okMap);
  if (!okMap) return 403;
  if (!isFile(fs)) return 404;
  if (::unlink(fs.c_str()) != 0) return 500;
  return 204;
}

} // namespace ws