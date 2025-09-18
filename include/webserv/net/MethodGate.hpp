#pragma once
#include <string>
#include "webserv/config/Config.hpp"

namespace ws {

/**
 * @brief Is method implemented by server core.
 * @param m method string (e.g. "GET").
 * @return true for GET/POST/DELETE/HEAD.
 */
bool isImplemented(const std::string& m);

/**
 * @brief Check allow_methods (HEAD is allowed if GET is present).
 * @param loc location (nullable).
 * @param method method to check ("GET" for HEAD check).
 * @return true if allowed.
 */
bool isAllowed(const ws::Location* loc, const std::string& method);

/**
 * @brief Compute Allow header value from location rules.
 * @param loc location (nullable).
 * @return e.g. "GET, POST, DELETE, HEAD".
 */
std::string buildAllowHeader(const ws::Location* loc);

} // namespace ws