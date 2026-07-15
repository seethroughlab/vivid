#include "persist_undo.h"

namespace vivid {

nlohmann::json canonical_document_projection(const nlohmann::json& session) {
    nlohmann::json j = session;   // copy; we prune in place
    if (!j.is_object()) return j;

    j.erase("window");   // view state

    if (auto git = j.find("graph"); git != j.end() && git->is_object()) {
        nlohmann::json& g = *git;
        g.erase("view");   // pan/zoom
        if (auto cit = g.find("chain"); cit != g.end() && cit->is_array()) {
            for (nlohmann::json& node : *cit) {
                if (!node.is_object()) continue;
                node.erase("base");   // legacy positional duplicate of "params"
                if (node.value("op_type", std::string()) == "Output") {
                    if (auto pit = node.find("params"); pit != node.end() && pit->is_object()) {
                        pit->erase("preview");
                        pit->erase("launch");
                        pit->erase("display");
                    }
                }
            }
        }
    }

    if (auto tit = j.find("tracks"); tit != j.end() && tit->is_array())
        for (nlohmann::json& tr : *tit) {
            if (!tr.is_object()) continue;
            tr.erase("active");   // the launched clip — performance state
            tr.erase("state");    // the plugin's opaque binary patch (non-deterministic getState())
            if (auto cit = tr.find("clap_effects"); cit != tr.end() && cit->is_array())
                for (nlohmann::json& fx : *cit)
                    if (fx.is_object()) fx.erase("state");
        }

    return j;
}

bool audio_block_equal(const nlohmann::json& a, const nlohmann::json& b) {
    // Compare the "tracks" arrays of two already-canonical projections. Absent == an empty array so a
    // session with no tracks compares equal to another with none.
    static const nlohmann::json kEmpty = nlohmann::json::array();
    const nlohmann::json& ta = a.contains("tracks") ? a.at("tracks") : kEmpty;
    const nlohmann::json& tb = b.contains("tracks") ? b.at("tracks") : kEmpty;
    return ta == tb;
}

namespace {
// A track with its VALUE fields removed, leaving only structural identity. The strip set here MUST
// match the value setters in persist.cpp's apply_track_values() — a field stripped here is a field
// ParamsOnly restore knows how to re-apply without a rebuild. Anything NOT stripped is treated as
// structure: differing => Full rebuild.
nlohmann::json track_topology(const nlohmann::json& tr) {
    nlohmann::json t = tr;
    if (!t.is_object()) return t;
    t.erase("gain"); t.erase("clips"); t.erase("trims"); t.erase("audio_clips");
    if (auto it = t.find("fx"); it != t.end() && it->is_array())
        for (auto& fx : *it) if (fx.is_object()) fx.erase("params");
    if (auto it = t.find("audio_instrument"); it != t.end() && it->is_object()) it->erase("params");
    if (auto it = t.find("audio_fx"); it != t.end() && it->is_array())
        for (auto& f : *it) if (f.is_object()) f.erase("params");
    if (auto ag = t.find("audio_graph"); ag != t.end() && ag->is_object())
        if (auto ns = ag->find("nodes"); ns != ag->end() && ns->is_array())
            for (auto& n : *ns)
                if (n.is_object()) { n.erase("params"); n.erase("key_lo"); n.erase("key_hi"); n.erase("x"); n.erase("y"); }
    return t;
}
}  // namespace

bool audio_topology_equal(const nlohmann::json& a, const nlohmann::json& b) {
    static const nlohmann::json kEmpty = nlohmann::json::array();
    const nlohmann::json& ta = a.contains("tracks") ? a.at("tracks") : kEmpty;
    const nlohmann::json& tb = b.contains("tracks") ? b.at("tracks") : kEmpty;
    if (!ta.is_array() || !tb.is_array() || ta.size() != tb.size()) return false;
    for (std::size_t i = 0; i < ta.size(); ++i)
        if (track_topology(ta[i]) != track_topology(tb[i])) return false;
    return true;
}

}  // namespace vivid
