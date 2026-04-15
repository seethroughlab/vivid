#pragma once

#include <atomic>
#include <cstdint>

// Lightweight exponential moving average. Single-writer; the atomic float
// allows a secondary thread (MCP / output_values) to read without locking.
struct StatEMA {
    std::atomic<float> value{0.0f};

    void update(float sample, float alpha = 0.05f) {
        float prev = value.load(std::memory_order_relaxed);
        value.store(prev + alpha * (sample - prev), std::memory_order_relaxed);
    }

    void reset() { value.store(0.0f, std::memory_order_relaxed); }
};

// Video-side playback counters. Single-threaded (GPU frame thread only).
// Reset on source change.
struct MovieVideoStats {
    uint64_t new_frame_count = 0;
    uint64_t reused_frame_count = 0;
    uint64_t nil_frame_count = 0;
    uint64_t gpu_native_frame_count = 0;
    uint64_t cpu_fallback_frame_count = 0;
    uint64_t metal_import_failure_count = 0;

    StatEMA decode_acquire_us;
    StatEMA decode_copy_us;
    StatEMA gpu_upload_us;
    StatEMA metal_blit_us;

    StatEMA drift_ms;
    uint64_t seek_correction_count = 0;
    uint64_t seek_budget_exhausted_count = 0;
    uint64_t drop_repeat_correction_count = 0;

    void reset() {
        new_frame_count = 0;
        reused_frame_count = 0;
        nil_frame_count = 0;
        gpu_native_frame_count = 0;
        cpu_fallback_frame_count = 0;
        metal_import_failure_count = 0;
        decode_acquire_us.reset();
        decode_copy_us.reset();
        gpu_upload_us.reset();
        metal_blit_us.reset();
        drift_ms.reset();
        seek_correction_count = 0;
        seek_budget_exhausted_count = 0;
        drop_repeat_correction_count = 0;
    }
};

// Audio-side playback counters. All atomic — written by audio thread,
// read by frame thread via analysis output port bridging.
struct MovieAudioStats {
    std::atomic<float> buffered_duration_ms{0.0f};
    std::atomic<uint64_t> underrun_count{0};

    void reset() {
        buffered_duration_ms.store(0.0f, std::memory_order_relaxed);
        underrun_count.store(0, std::memory_order_relaxed);
    }
};
