#pragma once
#include <string>

namespace ws {

/**
 * @brief Read entire file into string.
 * @param path filesystem path.
 * @param out destination buffer.
 * @return true on success.
 */
bool readWholeFile(const std::string& path, std::string& out);

/**
 * @brief Write binary file atomically (truncate).
 * @param path destination file path.
 * @param data bytes to write.
 * @return true on success.
 */
bool writeBinary(const std::string& path, const std::string& data);

/**
 * @brief Recursively create directories for a path like "a/b/c".
 * @param dir directory path.
 * @return true on success (or already exists).
 */
bool ensureDirRecursive(const std::string& dir);

} // namespace ws