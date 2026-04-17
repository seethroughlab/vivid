#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace vivid {

// SHA-256 of the input, returned as a lowercase 64-character hex string.
// Implementation follows FIPS 180-4. Not constant-time; intended for
// content hashing, not password storage or MAC use.
std::string sha256_hex(std::string_view input);

// SHA-256 of a file's raw byte contents. Returns a lowercase 64-char hex
// string on success, or an empty string on any I/O error (file missing,
// permission denied, etc.) — callers should treat "" as "no hash available"
// rather than retrying. Reads in 8 KB chunks so large files don't require
// proportional RAM.
std::string sha256_file(const std::filesystem::path& path);

}  // namespace vivid
