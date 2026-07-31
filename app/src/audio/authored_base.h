#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Ph5 audit P2-05: a hosted plugin's AUTHORED param base must survive a param re-cache.
//
// A plugin's live param table is compacted (hidden params are skipped) and re-read on every rescan /
// restartComponent, so a param's INDEX is not stable across a re-cache. The ADR-0030 authored base and
// its audio-thread mirror are index-aligned, so re-caching used to reset every authored value to "not
// authored" — silently losing what the user tuned (the "rescan nukes VST state" data loss).
//
// Keeping the authored values in a durable map keyed by STABLE param id, and re-applying them by id
// after each re-cache, makes them follow their param across a rescan / reorder / add / drop. Pure +
// header-only (no VST3/CLAP SDK) so the invariant is unit-testable.
namespace vivid::session {

// Rebuild the index-aligned (base, has) arrays for the NEW param order from the durable id->value map.
// A param whose id was authored gets its value + has=1; every other slot is zero/unauthored.
template <class IdT, class ValT>
void reapply_authored_base(const std::unordered_map<IdT, ValT>& authored,
                           const std::vector<IdT>& param_ids,
                           std::vector<ValT>& base_out,
                           std::vector<std::uint8_t>& has_out) {
    base_out.assign(param_ids.size(), ValT{});
    has_out.assign(param_ids.size(), std::uint8_t{0});
    if (authored.empty()) return;
    for (std::size_t i = 0; i < param_ids.size(); ++i) {
        auto it = authored.find(param_ids[i]);
        if (it != authored.end()) { base_out[i] = it->second; has_out[i] = std::uint8_t{1}; }
    }
}

}  // namespace vivid::session
