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

}  // namespace vivid
