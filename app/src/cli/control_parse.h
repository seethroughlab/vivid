#pragma once
#include <string>
#include <cstdlib>

// Pure parsing/validation helpers for the control server. Kept dependency-free
// (no app/GPU/audio types) so they are unit-testable headlessly — these encode
// real wire semantics (the mapping-source -> char_id scheme) where a bug would
// silently mis-route mappings.
namespace vivid::control {

// Half-open range check: is `i` a valid index into a container of `count`?
inline bool in_range(int i, int count) { return i >= 0 && i < count; }

// Characteristic kind -> column index. -1 if unknown.
inline int kind_index(const std::string& k) {
    if (k == "level")     return 0;
    if (k == "transient") return 1;
    if (k == "low")       return 2;
    if (k == "mid")       return 3;
    if (k == "high")      return 4;
    return -1;
}

// "master.<kind>" | "track_<n>.<kind>" -> char_id.
//   master = kind index (0..4); track t = 100 + t*8 + kind. -1 if malformed.
inline int char_id_from_source(const std::string& src) {
    const auto dot = src.find('.');
    if (dot == std::string::npos) return -1;
    const std::string head = src.substr(0, dot);
    const int ki = kind_index(src.substr(dot + 1));
    if (ki < 0) return -1;
    if (head == "master") return ki;
    if (head.rfind("track_", 0) == 0) return 100 + std::atoi(head.c_str() + 6) * 8 + ki;
    return -1;
}

}  // namespace vivid::control
