#pragma once

#include "ui/text_edit.h"
#include <string>
#include <cctype>
#include <climits>

namespace vivid::ui {

// Character filter predicates used across text fields
inline bool filter_printable(char c) {
    auto uc = static_cast<unsigned char>(c);
    return uc >= 32 && uc < 127;
}
inline bool filter_numeric(char c) {
    auto uc = static_cast<unsigned char>(c);
    return std::isdigit(uc) || c == '.' || c == '-';
}
inline bool filter_digits(char c) {
    return std::isdigit(static_cast<unsigned char>(c));
}
inline bool filter_identifier(char c) {
    auto uc = static_cast<unsigned char>(c);
    char lc = std::isupper(uc) ? static_cast<char>(std::tolower(uc)) : c;
    return std::islower(static_cast<unsigned char>(lc)) ||
           std::isdigit(static_cast<unsigned char>(lc)) || lc == '_';
}
inline bool filter_preset_name(char c) {
    auto uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_' || c == '/' || c == ' ' || c == '-';
}
inline bool filter_hex(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) || c == '#';
}
inline bool filter_rgb(char c) {
    return std::isdigit(static_cast<unsigned char>(c));
}

// Active text field resolution result
struct ActiveTextField {
    std::string* buf = nullptr;
    CharFilter filter;
    size_t max_len = SIZE_MAX;
    bool lowercase = false;  // auto-lowercase input (for identifier fields)
};

} // namespace vivid::ui
