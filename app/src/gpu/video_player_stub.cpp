#ifndef __APPLE__
#include "gpu/video_player.h"

// Video playback uses AVFoundation on macOS (video_player.mm). On other platforms it's
// disabled — a real backend (e.g. FFmpeg/libav) is a future P3 step. The C API is
// satisfied so the visuals/operator engine links + runs (the video source just yields
// no frames; image/test-pattern sources are unaffected).

VideoPlayer* video_open(const char* /*path*/) { return nullptr; }
void         video_close(VideoPlayer*) {}
void         video_play(VideoPlayer*, bool) {}
bool         video_next_frame(VideoPlayer*, const uint8_t**, uint32_t*, uint32_t*) { return false; }

#endif  // !__APPLE__
