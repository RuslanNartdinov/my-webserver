#include "webserv/utils/IO.hpp"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <fstream>
#include <sstream>
#include <string>

namespace ws {

bool readWholeFile(const std::string& p, std::string& out) {
  std::ifstream ifs(p.c_str(), std::ios::in | std::ios::binary);
  if (!ifs) return false;
  std::ostringstream oss;
  oss << ifs.rdbuf();
  out = oss.str();
  return true;
}

bool writeBinary(const std::string& path, const std::string& data) {
  int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
  if (fd < 0) return false;

  const char* p = data.data();
  size_t left = data.size();

  while (left) {
    ssize_t n = ::write(fd, p, left);
    if (n < 0) {
      int err = errno;          // сохраняем сразу!
      if (err == EINTR) continue;
      ::close(fd);
      return false;
    }
    p += n;
    left -= static_cast<size_t>(n);
  }

  ::close(fd);
  return true;
}

bool ensureDirRecursive(const std::string& dir) {
  if (dir.empty()) return false;

  std::string cur;
  for (size_t i = 0; i < dir.size(); ++i) {
    char ch = dir[i];
    cur.push_back(ch);

    if (ch == '/' || i + 1 == dir.size()) {
      if (cur == "/") continue;

      struct stat st;
      if (stat(cur.c_str(), &st) != 0) {
        if (::mkdir(cur.c_str(), 0755) != 0) {
          int err = errno;      // сохраняем сразу!
          if (err != EEXIST) return false;
          // EEXIST допустим, но проверим, что это именно директория
          if (stat(cur.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
            return false;
        }
      } else if (!S_ISDIR(st.st_mode)) {
        return false;
      }
    }
  }
  return true;
}

} // namespace ws