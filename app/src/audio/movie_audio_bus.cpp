// Host side of the movie-audio bus (see operator_api/movie_audio.h). A small fixed set of channels,
// each a lock-free stereo ring with a master clock. The Video op (render thread) WRITES its decoded
// movie audio to a channel; a MovieAudio audio-graph op (audio thread) DRAINS it via pull() so the
// sound flows through the graph's effects. The ring memory lives for the app lifetime — the audio
// thread never touches freed storage. RT-safe: no locks, no allocation on the audio path.
#include "operator_api/movie_audio.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

namespace {

// Lock-free SPSC stereo ring. read() advances read_head_time — the master A/V clock — by frames/sr,
// nudged toward (write_head_time − buffered_lag) so it tracks the true decoded position without
// jitter. Ported from vivid-classic's MovieFileRing.
struct Ring {
    static constexpr uint32_t kCapacity = 240000;   // ~5 s at 48 kHz
    std::array<float, kCapacity> left{};
    std::array<float, kCapacity> right{};
    std::atomic<uint32_t> write_pos{0};
    std::atomic<uint32_t> read_pos{0};
    std::atomic<uint32_t> epoch{0};
    std::atomic<float>    sample_rate{48000.0f};
    std::atomic<double>   read_head_time{0.0};
    std::atomic<double>   write_head_time{0.0};

    uint32_t available_read() const {
        return (write_pos.load(std::memory_order_acquire) - read_pos.load(std::memory_order_relaxed) + kCapacity) % kCapacity;
    }
    uint32_t available_write() const {
        return (read_pos.load(std::memory_order_acquire) - write_pos.load(std::memory_order_relaxed) - 1 + kCapacity) % kCapacity;
    }
    uint32_t write(const float* l, const float* r_in, uint32_t frames) {
        if (!l || !r_in || frames == 0) return 0;
        const uint32_t n = std::min(frames, available_write());
        const uint32_t wp = write_pos.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) { const uint32_t idx = (wp + i) % kCapacity; left[idx] = l[i]; right[idx] = r_in[i]; }
        write_pos.store((wp + n) % kCapacity, std::memory_order_release);
        return n;
    }
    uint32_t read(float* l_out, float* r_out, uint32_t frames) {
        if (!l_out || !r_out || frames == 0) return 0;
        const uint32_t epoch_before = epoch.load(std::memory_order_acquire);
        const uint32_t n = std::min(frames, available_read());
        const uint32_t rp = read_pos.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) { const uint32_t idx = (rp + i) % kCapacity; l_out[i] = left[idx]; r_out[i] = right[idx]; }
        if (epoch.load(std::memory_order_acquire) != epoch_before) {   // reset raced us — drop this block
            std::memset(l_out, 0, frames * sizeof(float));
            std::memset(r_out, 0, frames * sizeof(float));
            return 0;
        }
        if (n < frames) {
            std::memset(l_out + n, 0, (frames - n) * sizeof(float));
            std::memset(r_out + n, 0, (frames - n) * sizeof(float));
        }
        read_pos.store((rp + n) % kCapacity, std::memory_order_release);

        const double sr = std::max(1.0, static_cast<double>(sample_rate.load(std::memory_order_relaxed)));
        const double advance = static_cast<double>(frames) / sr;
        double new_t = read_head_time.load(std::memory_order_relaxed) + advance;
        const double wht = write_head_time.load(std::memory_order_acquire);
        const double buffer_lag = static_cast<double>(available_read()) / sr;
        const double expected_t = wht - buffer_lag;
        const double error = new_t - expected_t;
        if (std::abs(error) > 0.100)      new_t = expected_t + advance;   // hard resync
        else if (std::abs(error) > 0.002) new_t -= error * 0.10;          // gentle pull
        read_head_time.store(new_t, std::memory_order_relaxed);
        return n;
    }
    void clear(double reset_time) {
        write_pos.store(0, std::memory_order_relaxed);
        read_pos.store(0, std::memory_order_relaxed);
        read_head_time.store(reset_time, std::memory_order_relaxed);
        write_head_time.store(reset_time, std::memory_order_relaxed);
        epoch.fetch_add(1, std::memory_order_release);
    }
};

struct Channel {
    Ring                 ring;
    std::atomic<uint8_t> ever_drained{0};   // a MovieAudio op has drained this channel this file-load
};

std::array<Channel, VIVID_MOVIE_AUDIO_CHANNELS> g_channels;
std::atomic<int>   g_playing{0};
std::atomic<float> g_device_rate{48000.0f};

inline bool ok_channel(int c) { return c >= 0 && c < VIVID_MOVIE_AUDIO_CHANNELS; }

}  // namespace

extern "C" {

uint32_t vivid_movie_audio_write(int channel, const float* left, const float* right,
                                 uint32_t frames, double pts, float sample_rate) {
    if (!ok_channel(channel)) return 0;
    Ring& r = g_channels[channel].ring;
    r.sample_rate.store(sample_rate, std::memory_order_release);
    const uint32_t n = r.write(left, right, frames);
    r.write_head_time.store(pts, std::memory_order_release);
    return n;
}
double   vivid_movie_audio_read_head(int channel)     { return ok_channel(channel) ? g_channels[channel].ring.read_head_time.load(std::memory_order_acquire) : 0.0; }
uint32_t vivid_movie_audio_buffered(int channel)      { return ok_channel(channel) ? g_channels[channel].ring.available_read() : 0u; }
int      vivid_movie_audio_master_active(int channel) { return ok_channel(channel) ? g_channels[channel].ever_drained.load(std::memory_order_acquire) : 0; }
void     vivid_movie_audio_reset(int channel, double t) {
    if (!ok_channel(channel)) return;
    g_channels[channel].ring.clear(t);
    g_channels[channel].ever_drained.store(0, std::memory_order_release);
}

uint32_t vivid_movie_audio_pull(int channel, float* left, float* right, uint32_t frames) {
    if (!ok_channel(channel) || !left || !right) return 0;
    if (!g_playing.load(std::memory_order_acquire)) {   // transport paused: silence, freeze the clock
        std::memset(left, 0, frames * sizeof(float));
        std::memset(right, 0, frames * sizeof(float));
        return 0;
    }
    const uint32_t got = g_channels[channel].ring.read(left, right, frames);
    g_channels[channel].ever_drained.store(1, std::memory_order_release);
    return got;
}

void vivid_movie_audio_set_playing(int playing) { g_playing.store(playing, std::memory_order_release); }
void  vivid_movie_audio_set_device_rate(float sr) { if (sr > 0.f) g_device_rate.store(sr, std::memory_order_release); }
float vivid_movie_audio_device_rate(void) { return g_device_rate.load(std::memory_order_acquire); }

}  // extern "C"
