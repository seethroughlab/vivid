#pragma once
#include <cctype>
#include <string>

// Ranked substring matching, shared by every "type to filter a list" surface (the visuals Tab
// chooser, the audio Tab chooser, the PLUGINS browser search). Tiered rather than a plain
// substring test so an exact/prefix hit always outranks an incidental mid-string one — typing
// "mesh" must not bury Mesh under something whose summary merely mentions meshes.
//
// Lifted from vivid-classic's score_match_v2 (ui/graph/node_graph_util.h).
namespace vivid::ui {

inline std::string lower_str(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

// `label` = the primary name; `hay` = the pre-lowercased extra haystack (keywords, summary,
// vendor, format...). `filter` must already be lowercase. Returns < 0 for "no match".
inline int score_match(const std::string& label_lower, const std::string& hay_lower,
                       const std::string& filter) {
    if (filter.empty()) return 1;                                    // no filter: keep, natural order
    if (label_lower == filter) return 1000;                          // exact name
    if (label_lower.rfind(filter, 0) == 0) return 700;               // name prefix
    const std::size_t at = label_lower.find(filter);
    if (at != std::string::npos) return 400 - static_cast<int>(at);  // name substring (earlier wins)
    if (!hay_lower.empty() && hay_lower.find(filter) != std::string::npos) return 200;  // metadata hit
    return -1;
}

}  // namespace vivid::ui
