#ifndef __APPLE__
#include "platform/frame_loop.h"

namespace vivid {

// Generic frame loop for non-macOS platforms: pump events, then tick. No nested
// tracking-run-loop handling — hosting a native plugin GUI is a macOS-only feature, so
// a plain loop suffices for the visuals/operator engine.
void run_platform_frame_loop(std::function<bool()> poll_events,
                             std::function<bool()> tick) {
    while (true) {
        if (!poll_events()) break;
        if (!tick()) break;
    }
}

}  // namespace vivid
#endif  // !__APPLE__
