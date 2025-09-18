#pragma once
#include <string>
#include "webserv/http/Request.hpp"
#include "webserv/http/Router.hpp"

namespace ws {

/**
 * @brief Handle POST upload for a location with upload_enable + upload_store.
 *
 * On success:
 *  - writes file into <server.root>/<upload_store>/up_<...>
 *  - returns 201 and Location: /uploads/<name>
 *
 * @param route RouteMatch (server+location).
 * @param req   HttpRequest with body/headers.
 * @param outLocation Location header to set (e.g. "/uploads/xxx").
 * @return pair(status_code, body_text). status_code=0 means "not handled".
 */
std::pair<int,std::string> handleUpload(const ws::RouteMatch& route,
                                        const ws::HttpRequest& req,
                                        std::string& outLocation);

} // namespace ws