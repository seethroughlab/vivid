#include "cli/control_handlers_internal.h"

#include "ui/node_graph.h"   // NodeGraph::add_mapping / disconnect_dest

#include <string>

namespace vivid {

// The bridge: wire a characteristic source to a param destination (with amount/curve/invert/range),
// or tear a destination's mapping down.
void register_mappings_handlers(Handlers& handlers_) {
    handlers_["connect_mapping"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        const std::string src = b.value("src", std::string()), dst = b.value("dst", std::string());
        if (src.empty() || dst.empty()) return err(code::kBadArg, "need src and dst");
        c.graph->add_mapping(src, dst, b.value("amount", 1.0f), b.value("curve", 0.0f),
                             b.value("invert", false), b.value("lo", 0.0f), b.value("hi", 1.0f));
        return ok();
    };
    handlers_["disconnect_mapping"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        const std::string dst = b.value("dst", std::string());
        if (dst.empty()) return err(code::kBadArg, "need dst");
        c.graph->disconnect_dest(dst);
        return ok();
    };
}

}  // namespace vivid
