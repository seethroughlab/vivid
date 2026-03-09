#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

class AVFAudioExtractor;

namespace vivid {
namespace media {

enum class TransportCommandType : uint8_t {
    SetSource = 0,
    SetPlayback = 1,
    Seek = 2,
};

struct TransportCommand {
    TransportCommandType type = TransportCommandType::SetPlayback;
    uint64_t generation = 0;
    double seek_time_s = 0.0;
    float speed = 1.0f;
    uint8_t playing = 0;
    uint8_t loop_enabled = 0;
    std::string source_path;
};

struct VideoFrameEvent {
    uint64_t generation = 0;
    uint64_t frame_index = 0;
    double monotonic_time_s = 0.0;
    uint32_t width = 0;
    uint32_t height = 0;
};

enum class VideoFrameCompressionMode : uint8_t {
    UncompressedBGRA = 0,
    CompressedBC = 1,
};

struct VideoFramePayload {
    uint64_t generation = 0;
    uint64_t frame_index = 0;
    double monotonic_time_s = 0.0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0; // backend-specific enum value (e.g. WGPUTextureFormat)
    uint8_t ycocg_encoded = 0;
    VideoFrameCompressionMode compression_mode = VideoFrameCompressionMode::UncompressedBGRA;
    std::vector<uint8_t> bytes;
};

// Operator-layer shared media session payload.
// Stored behind runtime's generic shared-handle registry.
struct MediaSession {
    std::mutex mu;
    std::string source_path;

    std::atomic<uint64_t> source_generation{0};
    std::atomic<uint32_t> loop_epoch{0};
    std::atomic<double> monotonic_time_s{0.0};
    std::atomic<float> local_time_s{0.0f};
    std::atomic<float> duration_s{0.0f};
    std::atomic<float> speed{1.0f};
    std::atomic<uint8_t> playing{0};
    std::atomic<uint8_t> loop_enabled{0};
    std::atomic<uint64_t> generation_transitions{0};

    // Transport queue ownership lives in shared session layer so source/controller
    // and consumers can coordinate without runtime-specific media services.
    std::mutex transport_mu;
    std::deque<TransportCommand> transport_queue;
    uint64_t transport_serial = 0;

    // Shared ownership for audio extractor/ring-buffer path.
    std::mutex audio_owner_mu;
    std::shared_ptr<::AVFAudioExtractor> audio_extractor;
    static constexpr uint32_t kAudioRingCapacity = 96000;
    std::array<float, kAudioRingCapacity> audio_left{};
    std::array<float, kAudioRingCapacity> audio_right{};
    std::atomic<uint32_t> audio_write_pos{0};
    std::atomic<uint32_t> audio_read_pos{0};
    std::atomic<double> audio_read_head_media_time{0.0};
    std::atomic<float> audio_ring_sample_rate{48000.0f};
    std::atomic<float> audio_ring_speed{1.0f};
    std::atomic<uint64_t> audio_frames_written{0};
    std::atomic<uint64_t> audio_frames_read{0};
    std::atomic<uint64_t> audio_frames_discarded{0};
    std::atomic<uint64_t> audio_write_overflow_frames{0};
    std::atomic<uint64_t> audio_underrun_callbacks{0};
    std::atomic<uint64_t> audio_underrun_frames{0};
    std::atomic<uint32_t> audio_ring_depth_high_water{0};
    std::atomic<uint64_t> sync_resync_requests{0};
    std::atomic<uint64_t> sync_resync_applied{0};
    std::atomic<uint64_t> sync_skip_actions{0};
    std::atomic<uint64_t> sync_silence_actions{0};

    // Shared ownership for video-frame event queue metadata.
    std::mutex video_queue_mu;
    std::deque<VideoFrameEvent> video_queue;
    uint64_t video_frame_counter = 0;
    std::deque<VideoFramePayload> video_payload_queue;
    std::atomic<uint64_t> video_payload_enqueued{0};
    std::atomic<uint64_t> video_payload_popped{0};
    std::atomic<uint64_t> video_payload_dropped{0};
    std::atomic<uint32_t> video_payload_depth_high_water{0};
};

inline void media_session_update_high_water(std::atomic<uint32_t>& target, uint32_t value) {
    uint32_t cur = target.load(std::memory_order_relaxed);
    while (value > cur &&
           !target.compare_exchange_weak(cur, value,
                                         std::memory_order_relaxed,
                                         std::memory_order_relaxed)) {}
}

inline void media_session_note_generation_transition(MediaSession& s) {
    s.generation_transitions.fetch_add(1, std::memory_order_relaxed);
}

inline void media_session_enqueue_command(MediaSession& s, TransportCommand cmd) {
    std::lock_guard<std::mutex> lock(s.transport_mu);
    s.transport_queue.push_back(std::move(cmd));
    ++s.transport_serial;
}

inline std::optional<TransportCommand> media_session_pop_command(MediaSession& s) {
    std::lock_guard<std::mutex> lock(s.transport_mu);
    if (s.transport_queue.empty()) return std::nullopt;
    TransportCommand cmd = std::move(s.transport_queue.front());
    s.transport_queue.pop_front();
    return cmd;
}

inline void media_session_enqueue_video_frame(MediaSession& s, VideoFramePayload frame) {
    std::lock_guard<std::mutex> lock(s.video_queue_mu);
    s.video_payload_queue.push_back(std::move(frame));
    s.video_payload_enqueued.fetch_add(1, std::memory_order_relaxed);
    const uint32_t depth = static_cast<uint32_t>(s.video_payload_queue.size());
    media_session_update_high_water(s.video_payload_depth_high_water, depth);
    while (s.video_payload_queue.size() > 4) {
        s.video_payload_queue.pop_front();
        s.video_payload_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

inline std::optional<VideoFramePayload> media_session_pop_video_frame(MediaSession& s) {
    std::lock_guard<std::mutex> lock(s.video_queue_mu);
    if (s.video_payload_queue.empty()) return std::nullopt;
    VideoFramePayload out = std::move(s.video_payload_queue.front());
    s.video_payload_queue.pop_front();
    s.video_payload_popped.fetch_add(1, std::memory_order_relaxed);
    return out;
}

inline void media_session_audio_ring_clear(MediaSession& s) {
    s.audio_write_pos.store(0, std::memory_order_relaxed);
    s.audio_read_pos.store(0, std::memory_order_relaxed);
    s.audio_read_head_media_time.store(0.0, std::memory_order_relaxed);
}

inline uint32_t media_session_audio_available_read(const MediaSession& s) {
    const uint32_t w = s.audio_write_pos.load(std::memory_order_acquire);
    const uint32_t r = s.audio_read_pos.load(std::memory_order_relaxed);
    return (w - r + MediaSession::kAudioRingCapacity) % MediaSession::kAudioRingCapacity;
}

inline uint32_t media_session_audio_available_write(const MediaSession& s) {
    const uint32_t w = s.audio_write_pos.load(std::memory_order_relaxed);
    const uint32_t r = s.audio_read_pos.load(std::memory_order_acquire);
    return (r - w - 1 + MediaSession::kAudioRingCapacity) % MediaSession::kAudioRingCapacity;
}

inline uint32_t media_session_audio_write(MediaSession& s,
                                          const float* left,
                                          const float* right,
                                          uint32_t frames) {
    if (!left || !right || frames == 0) return 0;
    const uint32_t can_write = media_session_audio_available_write(s);
    const uint32_t to_write = std::min(frames, can_write);
    const uint32_t wp = s.audio_write_pos.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < to_write; ++i) {
        const uint32_t idx = (wp + i) % MediaSession::kAudioRingCapacity;
        s.audio_left[idx] = left[i];
        s.audio_right[idx] = right[i];
    }
    s.audio_write_pos.store((wp + to_write) % MediaSession::kAudioRingCapacity, std::memory_order_release);
    if (to_write > 0) {
        s.audio_frames_written.fetch_add(to_write, std::memory_order_relaxed);
        const uint32_t depth = media_session_audio_available_read(s);
        media_session_update_high_water(s.audio_ring_depth_high_water, depth);
    }
    if (to_write < frames) {
        s.audio_write_overflow_frames.fetch_add(frames - to_write, std::memory_order_relaxed);
    }
    return to_write;
}

inline uint32_t media_session_audio_read(MediaSession& s,
                                         float* left_out,
                                         float* right_out,
                                         uint32_t frames) {
    if (!left_out || !right_out || frames == 0) return 0;
    const uint32_t avail = media_session_audio_available_read(s);
    const uint32_t to_read = std::min(frames, avail);
    const uint32_t rp = s.audio_read_pos.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < to_read; ++i) {
        const uint32_t idx = (rp + i) % MediaSession::kAudioRingCapacity;
        left_out[i] = s.audio_left[idx];
        right_out[i] = s.audio_right[idx];
    }
    if (to_read < frames) {
        std::memset(left_out + to_read, 0, (frames - to_read) * sizeof(float));
        std::memset(right_out + to_read, 0, (frames - to_read) * sizeof(float));
        s.audio_underrun_callbacks.fetch_add(1, std::memory_order_relaxed);
        s.audio_underrun_frames.fetch_add(frames - to_read, std::memory_order_relaxed);
    }
    s.audio_read_pos.store((rp + to_read) % MediaSession::kAudioRingCapacity, std::memory_order_release);
    if (to_read > 0) {
        s.audio_frames_read.fetch_add(to_read, std::memory_order_relaxed);
    }
    if (to_read > 0) {
        const double sample_rate = std::max(1.0, static_cast<double>(s.audio_ring_sample_rate.load(std::memory_order_relaxed)));
        const double speed = std::max(0.0, static_cast<double>(s.audio_ring_speed.load(std::memory_order_relaxed)));
        const double advance = static_cast<double>(to_read) * speed / sample_rate;
        const double old_time = s.audio_read_head_media_time.load(std::memory_order_relaxed);
        s.audio_read_head_media_time.store(old_time + advance, std::memory_order_relaxed);
    }
    return to_read;
}

inline uint32_t media_session_audio_discard(MediaSession& s, uint32_t frames) {
    if (frames == 0) return 0;
    const uint32_t avail = media_session_audio_available_read(s);
    const uint32_t to_drop = std::min(frames, avail);
    const uint32_t rp = s.audio_read_pos.load(std::memory_order_relaxed);
    s.audio_read_pos.store((rp + to_drop) % MediaSession::kAudioRingCapacity, std::memory_order_release);
    if (to_drop > 0) {
        s.audio_frames_discarded.fetch_add(to_drop, std::memory_order_relaxed);
    }
    if (to_drop > 0) {
        const double sample_rate = std::max(1.0, static_cast<double>(s.audio_ring_sample_rate.load(std::memory_order_relaxed)));
        const double speed = std::max(0.0, static_cast<double>(s.audio_ring_speed.load(std::memory_order_relaxed)));
        const double advance = static_cast<double>(to_drop) * speed / sample_rate;
        const double old_time = s.audio_read_head_media_time.load(std::memory_order_relaxed);
        s.audio_read_head_media_time.store(old_time + advance, std::memory_order_relaxed);
    }
    return to_drop;
}

} // namespace media
} // namespace vivid
