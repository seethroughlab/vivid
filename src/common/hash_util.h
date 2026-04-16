#pragma once

#include <string>
#include <string_view>

namespace vivid {

// SHA-256 of the input, returned as a lowercase 64-character hex string.
// Implementation follows FIPS 180-4. Not constant-time; intended for
// content hashing, not password storage or MAC use.
std::string sha256_hex(std::string_view input);

}  // namespace vivid
