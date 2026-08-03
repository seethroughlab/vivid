// Core visual package operator: Output — the chain sink that feeds the viewer, and the OWNER of
// the output's identity (ADR-0014): its aspect ratio, its pixel size, how it fits a surface, and
// where it is shown. The host reads these params off the ACTIVE Output node and sizes every render
// target from them (VisualGraph::apply_output_settings).
//
// The op itself is still a passthrough blit; the params carry no GPU work of their own.
//
// COUPLING: the enum LABELS below must stay in step with the host's tables in gpu/output_format.h
// (kFitLabels / kAspectLabels / kHeightLabels). The host maps the chosen INDEX to a fit mode /
// ratio / pixel height, so adding a label on one side without the other silently changes meaning.
#include "blit_op.h"
#include <array>

struct OutputOp : core_visuals::BlitOp {
    static constexpr const char* kName = "Output";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SINK;   // ADR-0046
    static constexpr const char* kDisplayName = "Output";
    static constexpr const char* kSummary = "Chain sink: owns the output's size, aspect and fit; feeds the viewer.";
    static constexpr std::array<const char*, 3> kKeywords = {"output", "viewer", "sink"};
    OutputOp() : BlitOp("Output") {}

    vivid::Param<int> fit{"fit", 0, {"Fit", "Fill", "Stretch"}};
    vivid::Param<int> aspect{"aspect", 0, {"16:9", "4:3", "1:1", "9:16", "21:9", "16:10"}};
    vivid::Param<int> height{"height", 2, {"360", "540", "720", "1080", "1440", "2160"}};   // 2 = 720
    // Where the output is SHOWN. `preview` is the floating panel over the graph; `launch` opens the
    // output in its own window on `display` (the performance screen). The frame loop reconciles
    // these each tick, and writes `launch` back to 0 if that window is closed from the OS side, so
    // the node can never claim a window that isn't there.
    vivid::Param<bool> preview{"preview", true};
    vivid::Param<bool> launch{"launch", false};
    vivid::Param<int>  display{"display", 0, {"Current", "Primary", "Secondary"}};

    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&fit); o.push_back(&aspect); o.push_back(&height);
        o.push_back(&preview); o.push_back(&launch); o.push_back(&display);
    }
};

VIVID_REGISTER(OutputOp)
