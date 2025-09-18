#pragma once
#include <string>

namespace ws {

/**
 * @brief Build HTTP response start-line + headers.
 *
 * Sets: Server, Date, Content-Type, Content-Length, Connection,
 * optional Keep-Alive, optional Location, and appends extra headers.
 *
 * @param code    HTTP status code.
 * @param reason  Reason phrase.
 * @param ctype   MIME type.
 * @param clen    Content-Length.
 * @param keepAlive true for Connection: keep-alive (+Keep-Alive header).
 * @param location Optional Location header.
 * @param dateStr  Preformatted Date (IMF-fixdate).
 * @param extra   Extra header lines, each ending with "\r\n".
 * @return Header block (ending with double CRLF).
 */
std::string buildHeaders(int code,
                         const std::string& reason,
                         const std::string& ctype,
                         size_t clen,
                         bool keepAlive,
                         const std::string& location,
                         const std::string& dateStr,
                         const std::string& extra);

/**
 * @brief Append a single-chunk body using chunked encoding.
 * @param out buffer to append to (must already contain headers).
 * @param body payload to send in one chunk.
 */
void appendChunked(std::string& out, const std::string& body);

} // namespace ws