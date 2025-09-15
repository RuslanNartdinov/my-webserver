#include "webserv/http/Router.hpp"
#include <cstddef>

namespace ws {

Router::Router(const Config* cfg) : _cfg(cfg) {}

// Нормализуем host из заголовка: отрезаем порт, приводим к точному сравнению
static std::string hostFromHeader(const std::string& hostHeader) {
    if (hostHeader.empty()) return std::string();
    std::string h = hostHeader;
    // отрежем :port если есть
    std::string::size_type colon = h.rfind(':');
    if (colon != std::string::npos) {
        // ipv6 не поддерживаем здесь — у нас IPv4; простое правило достаточно
        h = h.substr(0, colon);
    }
    return h;
}

const ServerConfig* Router::pickServer(const std::string& lhost, int lport,
                                       const std::string& hostHeader) const
{
    // 1) среди серверов, слушающих тот же host:port
    const ServerConfig* firstForPair = 0;
    std::string hh = hostFromHeader(hostHeader);

    for (size_t i=0; i<_cfg->servers.size(); ++i) {
        const ServerConfig& s = _cfg->servers[i];
        if (s.host == lhost && s.port == lport) {
            if (!firstForPair) firstForPair = &s;
            // если Host совпал с одним из server_name — берём его
            for (size_t j=0; j<s.server_names.size(); ++j) {
                if (!hh.empty() && s.server_names[j] == hh) return &s;
            }
        }
    }
    // 2) иначе — первый server для пары (default_server)
    if (firstForPair) return firstForPair;

    // крайний случай (не должен случиться при валидной конфигурации)
    return _cfg->servers.empty() ? 0 : &_cfg->servers[0];
}

static std::string pathOnly(const std::string& target) {
    // отделим путь от ?query
    std::string::size_type q = target.find('?');
    return q == std::string::npos ? target : target.substr(0, q);
}

const Location* Router::pickLocation(const ServerConfig* srv,
                                     const std::string& path) const
{
    const Location* best = 0;
    size_t bestLen = 0;
    for (size_t i=0; i<srv->locations.size(); ++i) {
        const Location& L = srv->locations[i];
        const std::string& p = L.path;
        if (p.size() <= path.size() && path.compare(0, p.size(), p) == 0) {
            if (p.size() > bestLen) { best = &L; bestLen = p.size(); }
        }
    }
    return best;
}

RouteMatch Router::resolve(const std::string& listenerHost, int listenerPort,
                           const std::string& hostHeader,
                           const std::string& requestTarget) const
{
    RouteMatch r; r.server = 0; r.location = 0;
    r.server = pickServer(listenerHost, listenerPort, hostHeader);
    if (r.server) {
        std::string p = pathOnly(requestTarget);
        r.location = pickLocation(r.server, p);
    }
    return r;
}

} // namespace ws