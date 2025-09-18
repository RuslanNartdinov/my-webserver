#include "webserv/net/UploadHandler.hpp"
#include "webserv/utils/IO.hpp"
#include "webserv/fs/Path.hpp"
#include <sstream>
#include <ctime>
#include <unistd.h>
#include <sys/stat.h>

namespace ws {

static std::string genUploadName() {
  std::ostringstream oss;
  oss << "up_" << (unsigned long long)time(0)
      << "_" << (unsigned long long)getpid()
      << "_" << (unsigned long long)rand();
  return oss.str();
}

std::pair<int,std::string> handleUpload(const ws::RouteMatch& m,
                                        const ws::HttpRequest& req,
                                        std::string& outLocation) {
  if (req.method != "POST" || !m.location || !m.location->upload_enable || m.location->upload_store.empty())
    return std::make_pair(0, std::string());

  // size limit: loc > server > 10M
  size_t limit = 10 * 1024 * 1024;
  if (m.location->client_max_body_size) limit = m.location->client_max_body_size;
  else if (m.server && m.server->client_max_body_size) limit = m.server->client_max_body_size;

  if (req.body.size() > limit)
    return std::make_pair(413, "Payload Too Large\n");

  std::string serverRoot = (m.server && !m.server->root.empty()) ? m.server->root : std::string(".");
  std::string updir = m.location->upload_store; // e.g. "./uploads"
  if (!updir.empty() && updir[0] != '/')
    updir = (serverRoot.back() == '/' ? serverRoot + updir : serverRoot + "/" + updir);

  if (!ensureDirRecursive(updir))
    return std::make_pair(500, "Internal Server Error\n");

  const std::string fileName = genUploadName();
  const std::string outPath = updir + "/" + fileName;
  if (!writeBinary(outPath, req.body))
    return std::make_pair(500, "Internal Server Error\n");

  outLocation = "/uploads/" + fileName; // public URL expected by tests
  return std::make_pair(201, "201 Created\n");
}

} // namespace ws