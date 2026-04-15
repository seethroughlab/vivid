#pragma once

#include <string>
#include <cstdint>
#include <cstdio>
#include <cctype>

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

inline std::string strip_cadence_suffix(const std::string& name) {
    if (name.size() > 2) {
        auto suffix = name.substr(name.size() - 2);
        if (suffix == "Au" || suffix == "Fr")
            return name.substr(0, name.size() - 2);
    }
    return name;
}

inline std::string camel_case_initials(const std::string& name) {
    std::string stripped = strip_cadence_suffix(name);
    if (stripped.empty()) return "??";

    // Collect uppercase letter positions
    std::string uppers;
    for (char c : stripped) {
        if (std::isupper(static_cast<unsigned char>(c)))
            uppers += c;
    }

    if (uppers.size() >= 2) {
        return uppers.substr(0, 2);
    }
    // Single uppercase or none: return first char (uppercased) + second char (lowercased)
    char first = static_cast<char>(std::toupper(static_cast<unsigned char>(stripped[0])));
    if (stripped.size() >= 2) {
        char second = static_cast<char>(std::tolower(static_cast<unsigned char>(stripped[1])));
        return std::string(1, first) + second;
    }
    return std::string(1, first);
}

} // namespace vivid
