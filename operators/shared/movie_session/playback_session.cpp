#include "playback_session.h"

PlaybackSession::PlaybackSession(const std::string& source_path)
    : source_path_(source_path) {}

const std::string& PlaybackSession::source_path() const { return source_path_; }
MovieTransport& PlaybackSession::transport() { return transport_; }
const MovieTransport& PlaybackSession::transport() const { return transport_; }

void PlaybackSession::acquire() {
    ref_count_.fetch_add(1, std::memory_order_relaxed);
}

void PlaybackSession::release() {
    ref_count_.fetch_sub(1, std::memory_order_relaxed);
}

int PlaybackSession::ref_count() const {
    return ref_count_.load(std::memory_order_relaxed);
}
