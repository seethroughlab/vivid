#pragma once

#include "playback_session.h"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// Global singleton registry for PlaybackSession objects.
//
// Lives in the movie_session shared dylib so that both movie_file_in.dylib
// and movie_file_audio.dylib share the same registry instance.  When both
// operators open the same file path, they get the same PlaybackSession.
//
// Thread safety: the registry mutex protects the map.  Individual
// MovieTransport methods are single-threaded (called from the frame thread).
class PlaybackSessionRegistry {
public:
    static PlaybackSessionRegistry& instance();

    // Get or create a session for the given source path.
    // Increments the session's ref count.
    std::shared_ptr<PlaybackSession> acquire(const std::string& path);

    // Release a session.  Removes it from the registry when ref_count hits 0.
    void release(const std::string& path);

private:
    PlaybackSessionRegistry() = default;
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<PlaybackSession>> sessions_;
};
