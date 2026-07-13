// Core visual package operator: Output — the chain sink that feeds the on-screen
// viewer. The host identifies the active output by the name-derived VOp::Output and
// presents the texture flowing INTO it; this op is a passthrough blit. Migrated from
// the built-in OutputOp (a BlitOp); behaviour unchanged.
#include "blit_op.h"
#include <array>

struct OutputOp : core_visuals::BlitOp {
    static constexpr const char* kName = "Output";
    static constexpr const char* kDisplayName = "Output";
    static constexpr const char* kSummary = "Chain sink: feeds the connected texture to the on-screen viewer.";
    static constexpr std::array<const char*, 3> kKeywords = {"output", "viewer", "sink"};
    OutputOp() : BlitOp("Output") {}
};

VIVID_REGISTER(OutputOp)
