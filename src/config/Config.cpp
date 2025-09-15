#include "webserv/config/Config.hpp"
#include <cctype>
#include <cstdlib>

namespace ws {

static bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

size_t parseSizeWithUnits(const std::string& s, size_t ln, size_t col) {
    // поддержка: 123, 10k, 10K, 2m, 1g (десятичные или двоичные — примем двоичные)
    if (s.empty()) throw ConfigError("empty size", ln, col);
    std::string num = s;
    size_t mult = 1;
    if (endsWith(s, "k") || endsWith(s, "K")) { mult = 1024; num = s.substr(0, s.size()-1); }
    else if (endsWith(s, "m") || endsWith(s, "M")) { mult = 1024*1024; num = s.substr(0, s.size()-1); }
    else if (endsWith(s, "g") || endsWith(s, "G")) { mult = 1024ULL*1024ULL*1024ULL; num = s.substr(0, s.size()-1); }

    for (size_t i=0;i<num.size();++i) if (!std::isdigit(num[i])) {
        throw ConfigError("invalid size number: " + s, ln, col);
    }
    size_t base = static_cast<size_t>(std::strtoull(num.c_str(), 0, 10));
    return base * mult;
}

} // namespace ws