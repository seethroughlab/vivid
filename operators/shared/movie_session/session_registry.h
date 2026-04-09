#pragma once

#include "playback_session.h"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// Global singleton registry for PlaybackSession objects.
//
// Lives in the movie_session shared dylib so that operator modules share one
// registry instance while keeping playback sessions scoped to an operator
// instance rather than a media path.
//
// Thread safety: the registry mutex protects the map.  Individual
// MovieTransport methods are single-threaded (called from the frame thread).
class PlaybackSessionRegistry {
public:
    static PlaybackSessionRegistry& instance();

    // Get or create a session for the given operator node id.
    // Increments the session's ref count.
    std::shared_ptr<PlaybackSession> acquire(const std::string& operator_id,
                                             const std::string& source_path);

    // Release a session.  Removes it from the registry when ref_count hits 0.
    void release(const std::string& operator_id);

private:
    PlaybackSessionRegistry() = default;
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<PlaybackSession>> sessions_;
};
