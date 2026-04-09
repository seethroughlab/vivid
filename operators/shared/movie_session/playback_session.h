#pragma once

#include "movie_transport.h"
#include <atomic>
#include <string>

// A per-source playback session shared between MovieFileIn and MovieFileAudio.
// Owns a MovieTransport that centralizes transport time and seek policy.
// Instances are managed by PlaybackSessionRegistry.
class PlaybackSession {
public:
    explicit PlaybackSession(const std::string& source_path);

    const std::string& source_path() const;
    MovieTransport& transport();
    const MovieTransport& transport() const;

    void acquire();
    void release();
    int ref_count() const;

private:
    std::string source_path_;
    MovieTransport transport_;
    std::atomic<int> ref_count_{0};
};
