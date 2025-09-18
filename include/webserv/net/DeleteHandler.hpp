#pragma once
#include <string>
#include "webserv/http/Request.hpp"
#include "webserv/http/Router.hpp"

namespace ws {

/**
 * @brief Handle DELETE for a file under mapped server/location root.
 *
 * Blocks traversal, maps alias/root safely, ensures path stays under base.
 *
 * @param route RouteMatch with server/location chosen.
 * @param req   HttpRequest (uses raw_target / target).
 * @return HTTP status code (204, 403, 404, 500). 0 means "not handled".
 */
int handleDelete(const ws::RouteMatch& route, const ws::HttpRequest& req);

} // namespace ws