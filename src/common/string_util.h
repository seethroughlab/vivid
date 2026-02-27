#pragma once

#include <string>
#include <cstdio>

namespace vivid {

inline std::string format_float(float v, int precision = 4) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, v);
    return buf;
}

inline std::string format_uint(uint32_t v) {
    return std::to_string(v);
}

inline std::string format_int(int v) {
    return std::to_string(v);
}

} // namespace vivid
