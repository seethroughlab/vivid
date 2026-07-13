#pragma once
#include "audio/plugin_catalog.h"
#include "ui/text_match.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

// The PLUGINS browser's row list: the catalog, filtered by the search box and ranked. Shared by
// the draw (ui/session_view.cpp) and the hit-test (app/input_plugins.cpp) so a row's screen
// position and the catalog index behind it can never disagree — the same rule as ui/layout.h.
//
// Returns CATALOG indices, in display order. An empty filter = everything, in catalog (name) order.
namespace vivid::ui {

inline std::vector<int> plugin_browser_rows(const std::string& filter) {
    namespace S = vivid::session;
    const std::string f = lower_str(filter);
    std::vector<std::pair<int, int>> scored;   // (score, catalog index)
    const int n = S::plugin_count();
    scored.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const S::PluginInfo& p = S::plugin_at(i);
        // Searchable metadata: vendor, format ("vst3"/"clap") and class ("instrument"/"effect"),
        // so "clap" or "synth"-ish queries find things the bundle name doesn't mention.
        const std::string hay = lower_str(p.vendor + " " + S::plugin_format_name(p.format) + " " +
                                          S::plugin_class_name(p.cls));
        const int sc = score_match(lower_str(p.name), hay, f);
        if (sc >= 0) scored.push_back({ sc, i });
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;   // best score first; ties keep catalog (alphabetical) order
    });
    std::vector<int> rows;
    rows.reserve(scored.size());
    for (const auto& s : scored) rows.push_back(s.second);
    return rows;
}

}  // namespace vivid::ui
