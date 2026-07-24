#pragma once
// Sample-playback data model + region lookup. Ported from vivid-classic
// (operators/shared/sampler_common/sample_bank.h), split so that the pure data
// structures carry NO decoder dependency: this header has no miniaudio/JSON, so
// the voice engine (voice.h) and the Sampler op can include it without pulling
// the audio-file decoder into every translation unit. Decoding lives in
// sample_decode.h (miniaudio), which only the file-load runtime includes.
#include <climits>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vivid {
namespace sample_engine {

struct SampleData {
    std::vector<float> samples_L;   // left channel (or mono)
    std::vector<float> samples_R;   // right channel (empty if mono)
    uint32_t sample_rate = 0;
    bool     stereo = false;
    float    tempo_bpm = 0.0f;      // from ACID chunk / filename; 0 if unknown
};

struct SampleRegion {
    int   root_note = 60;           // MIDI note the sample was recorded at
    int   lo_note = 0;              // note range lower bound
    int   hi_note = 127;            // note range upper bound
    int   lo_vel = 0;              // velocity range lower bound (0–127)
    int   hi_vel = 127;            // velocity range upper bound (0–127)
    float volume_db = 0.0f;        // per-region volume adjustment
    float pan = 0.0f;              // stereo pan (-1.0 .. 1.0)
    int   tune_cents = 0;          // fine tuning in cents
    bool     loop_enabled = false;
    uint32_t loop_start = 0;       // loop start frame
    uint32_t loop_end = 0;         // loop end frame
    uint32_t loop_crossfade = 0;   // crossfade length in frames
    std::shared_ptr<SampleData> data;
};

struct SampleGroup {
    std::string name;
    int   keyswitch = -1;          // MIDI note that activates this group (-1 = none)
    float attack = 0.001f;
    float decay = 0.0f;
    float sustain = 1.0f;
    float release = 0.01f;
    std::vector<SampleRegion> regions;
};

struct SampleBank {
    std::string name;
    std::vector<SampleGroup> groups;
    float attack = 0.001f;
    float decay = 0.0f;
    float sustain = 1.0f;
    float release = 0.01f;
};

inline float db_to_linear(float db) { return std::pow(10.0f, db / 20.0f); }

// ---- Region lookup -------------------------------------------------------

inline const SampleRegion* find_region(const SampleGroup& group, int note, int velocity) {
    for (const auto& r : group.regions)
        if (note >= r.lo_note && note <= r.hi_note &&
            velocity >= r.lo_vel && velocity <= r.hi_vel)
            return &r;
    return nullptr;
}

// Overload: velocity as 0.0–1.0 float, scaled to 0–127.
inline const SampleRegion* find_region(const SampleGroup& group, int note, float velocity) {
    int vel127 = static_cast<int>(velocity * 127.0f);
    if (vel127 < 0)   vel127 = 0;
    if (vel127 > 127) vel127 = 127;
    return find_region(group, note, vel127);
}

// Fallback: the region whose root_note is closest to the requested note.
inline const SampleRegion* find_nearest_region(const SampleGroup& group, int note) {
    const SampleRegion* best = nullptr;
    int best_dist = INT_MAX;
    for (const auto& r : group.regions) {
        if (!r.data) continue;
        const int dist = std::abs(note - r.root_note);
        if (dist < best_dist) { best_dist = dist; best = &r; }
    }
    return best;
}

} // namespace sample_engine
} // namespace vivid
