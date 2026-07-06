#pragma once
#include <cstdint>

// A minimal real-time video player (AVFoundation): plays + loops a file and hands
// back the current frame as tightly-packed BGRA8. Main-thread use only. Feeds a
// TextureSource — the video counterpart of an image/test-pattern source.
struct VideoPlayer;

VideoPlayer* video_open(const char* path);
void         video_close(VideoPlayer*);
void         video_play(VideoPlayer*, bool playing);

// If a new frame is ready for "now", fills a BGRA8 buffer (valid until the next
// call) + its dimensions and returns true; otherwise returns false.
bool video_next_frame(VideoPlayer*, const uint8_t** out_bgra, uint32_t* w, uint32_t* h);
