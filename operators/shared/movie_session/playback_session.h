#pragma once

#include "movie_transport.h"
#include <atomic>
#include <string>

// A per-operator playback session.
// Owns a MovieTransport that centralizes transport time and seek policy.
// Instances are managed by PlaybackSessionRegistry and keyed by operator node id.
class PlaybackSession {
public:
    PlaybackSession(std::string operator_id, std::string source_path);

    const std::string& operator_id() const;
    const std::string& source_path() const;
    void set_source_path(std::string source_path);
    MovieTransport& transport();
    const MovieTransport& transport() const;

    void acquire();
    void release();
    int ref_count() const;

private:
    std::string operator_id_;
    std::string source_path_;
    MovieTransport transport_;
    std::atomic<int> ref_count_{0};
};
