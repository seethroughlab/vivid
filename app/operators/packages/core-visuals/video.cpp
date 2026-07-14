// Core visual package operator: Video — plays the shared image/video source texture
// into the chain. The host injects its decoded source into this node's input port 0
// (keyed on the op type name — VisualNode::is_video, a host contract); this op just
// blits it. Migrated from the built-in VideoOp (a BlitOp); behaviour unchanged.
#include "blit_op.h"
#include <array>

struct VideoOp : core_visuals::BlitOp {
    static constexpr const char* kName = "Video";
    static constexpr const char* kDisplayName = "Video";
    static constexpr const char* kSummary = "Plays the shared image/video source texture into the chain.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "video", "source"};
    VideoOp() : BlitOp("Video") {}
};

VIVID_REGISTER(VideoOp)
