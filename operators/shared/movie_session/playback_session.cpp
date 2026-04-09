#include "playback_session.h"

#include <utility>

PlaybackSession::PlaybackSession(std::string operator_id, std::string source_path)
    : operator_id_(std::move(operator_id)), source_path_(std::move(source_path)) {}

const std::string& PlaybackSession::operator_id() const { return operator_id_; }
const std::string& PlaybackSession::source_path() const { return source_path_; }
void PlaybackSession::set_source_path(std::string source_path) { source_path_ = std::move(source_path); }
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
