#pragma once
#include <string>
#include <ctime>

namespace ws {

/**
 * @brief RFC7231 IMF-fixdate for current time (GMT).
 * @return Formatted date string, e.g. "Thu, 18 Sep 2025 07:55:01 GMT".
 */
std::string httpDateNow();

/**
 * @brief RFC7231 IMF-fixdate for given time (GMT).
 * @param t epoch seconds (time_t).
 * @return Formatted date string.
 */
std::string httpDateFrom(time_t t);

/**
 * @brief Parse RFC7231 IMF-fixdate (primary format) into epoch seconds.
 * @param s date like "Tue, 15 Nov 1994 08:12:31 GMT".
 * @return epoch seconds, or (time_t)-1 if parsing failed.
 */
time_t parseHttpDate(const std::string& s);

} // namespace ws