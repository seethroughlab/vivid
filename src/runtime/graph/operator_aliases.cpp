#include "runtime/graph/operator_aliases.h"

#include <string_view>
#include <utility>
#include <vector>

namespace vivid {

namespace {

struct ParamInject {
    std::string_view key;
    float value;
};

struct ParamRename {
    std::string_view from;
    std::string_view to;
};

struct ParamOffset {
    std::string_view key;
    float add;
};

struct AliasEntry {
    std::string_view new_type;
    std::vector<ParamInject> inject;
    std::vector<ParamRename> rename;
    std::vector<ParamOffset> offset;  // Applied AFTER rename, only if key is already present.
};

// Legacy operator-id migration table. Add an entry when an operator is renamed
// or absorbed into another so graph JSON saved against the old id continues to
// load. Keys/values use the operator's `kName` (CapitalCase) — what graph JSON
// stores under `"type": "..."` (not the snake_case cmake target).
//
// Entry shapes:
//   Pure rename:                {"OldName", {"NewName", {}, {}, {}}}
//   Rename + inject mode param: {"OldName", {"NewName", {{"mode", 1.0f}}, {}, {}}}
//   Rename a param key:         {"OldName", {"NewName", {}, {{"old_p", "new_p"}}, {}}}
//   Offset a param value:       {"OldName", {"NewName", {}, {}, {{"index", 7.0f}}}}
const std::unordered_map<std::string, AliasEntry>& alias_table() {
    static const std::unordered_map<std::string, AliasEntry> table = {
        // (no aliases yet)
    };
    return table;
}

}  // namespace

std::string resolve_operator_alias(
    const std::string& raw_type,
    std::unordered_map<std::string, float>& params,
    std::unordered_map<std::string, std::string>& string_params) {

    const auto& table = alias_table();
    auto it = table.find(raw_type);
    if (it == table.end()) return raw_type;

    const auto& entry = it->second;

    for (const auto& r : entry.rename) {
        std::string from(r.from);
        std::string to(r.to);
        if (auto p = params.find(from); p != params.end()) {
            params.emplace(std::move(to), p->second);
            params.erase(p);
        } else if (auto sp = string_params.find(from); sp != string_params.end()) {
            string_params.emplace(std::move(to), std::move(sp->second));
            string_params.erase(sp);
        }
    }

    for (const auto& inj : entry.inject) {
        std::string key(inj.key);
        if (params.find(key) == params.end()) {
            params.emplace(std::move(key), inj.value);
        }
    }

    for (const auto& off : entry.offset) {
        if (auto p = params.find(std::string(off.key)); p != params.end()) {
            p->second += off.add;
        }
    }

    return std::string(entry.new_type);
}

}  // namespace vivid
