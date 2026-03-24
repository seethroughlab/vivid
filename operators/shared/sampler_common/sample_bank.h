#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdio>
#include <cmath>
#include <fstream>
#include "miniaudio.h"
#include <nlohmann/json.hpp>

namespace vivid_sampler {

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

struct SampleData {
    std::vector<float> samples_L;   // left channel (or mono)
    std::vector<float> samples_R;   // right channel (empty if mono)
    uint32_t sample_rate = 0;
    bool stereo = false;
};

struct SampleRegion {
    int root_note = 60;         // MIDI note the sample was recorded at
    int lo_note = 0;            // note range lower bound
    int hi_note = 127;          // note range upper bound
    int lo_vel = 0;             // velocity range lower bound (0–127)
    int hi_vel = 127;           // velocity range upper bound (0–127)
    float volume_db = 0.0f;     // per-region volume adjustment
    float pan = 0.0f;           // stereo pan (-1.0 to 1.0)
    int tune_cents = 0;         // fine tuning in cents
    bool loop_enabled = false;
    uint32_t loop_start = 0;    // loop start frame
    uint32_t loop_end = 0;      // loop end frame
    uint32_t loop_crossfade = 0;// crossfade length in frames
    std::shared_ptr<SampleData> data;
};

struct SampleGroup {
    std::string name;
    int keyswitch = -1;         // MIDI note that activates this group (-1 = none)
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

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

inline float db_to_linear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// ---------------------------------------------------------------------------
// WAV decoding via miniaudio
// ---------------------------------------------------------------------------

inline std::shared_ptr<SampleData> decode_wav(const std::string& path) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;

    ma_result result = ma_decoder_init_file(path.c_str(), &config, &decoder);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "[vivid-sampler] Failed to decode: %s (error %d)\n",
                path.c_str(), result);
        return nullptr;
    }

    ma_uint64 total_frames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames);
    if (total_frames == 0) {
        fprintf(stderr, "[vivid-sampler] Empty or unreadable: %s\n", path.c_str());
        ma_decoder_uninit(&decoder);
        return nullptr;
    }

    uint32_t channels = decoder.outputChannels;
    bool stereo = (channels >= 2);

    // Read interleaved frames
    std::vector<float> interleaved(static_cast<size_t>(total_frames) * channels);
    ma_uint64 frames_read = 0;
    ma_decoder_read_pcm_frames(&decoder, interleaved.data(), total_frames, &frames_read);
    ma_decoder_uninit(&decoder);

    auto sample = std::make_shared<SampleData>();
    sample->sample_rate = decoder.outputSampleRate;
    sample->stereo = stereo;

    if (stereo) {
        sample->samples_L.resize(static_cast<size_t>(frames_read));
        sample->samples_R.resize(static_cast<size_t>(frames_read));
        for (size_t i = 0; i < frames_read; ++i) {
            sample->samples_L[i] = interleaved[i * channels];
            sample->samples_R[i] = interleaved[i * channels + 1];
        }
    } else {
        sample->samples_L.resize(static_cast<size_t>(frames_read));
        for (size_t i = 0; i < frames_read; ++i) {
            sample->samples_L[i] = interleaved[i * channels];
        }
    }

    fprintf(stderr, "[vivid-sampler] Decoded: %s (%llu frames, %u Hz, %s)\n",
            path.c_str(), (unsigned long long)frames_read,
            sample->sample_rate, stereo ? "stereo" : "mono");

    return sample;
}

// ---------------------------------------------------------------------------
// Region lookup
// ---------------------------------------------------------------------------

inline const SampleRegion* find_region(const SampleGroup& group, int note, int velocity) {
    for (const auto& r : group.regions) {
        if (note >= r.lo_note && note <= r.hi_note &&
            velocity >= r.lo_vel && velocity <= r.hi_vel) {
            return &r;
        }
    }
    return nullptr;
}

// Overload: velocity as 0.0–1.0 float, scaled to 0–127
inline const SampleRegion* find_region(const SampleGroup& group, int note, float velocity) {
    int vel127 = static_cast<int>(velocity * 127.0f);
    if (vel127 < 0) vel127 = 0;
    if (vel127 > 127) vel127 = 127;
    return find_region(group, note, vel127);
}

// Fallback: find the region whose root_note is closest to the requested note
inline const SampleRegion* find_nearest_region(const SampleGroup& group, int note) {
    const SampleRegion* best = nullptr;
    int best_dist = INT_MAX;
    for (const auto& r : group.regions) {
        if (!r.data) continue;
        int dist = std::abs(note - r.root_note);
        if (dist < best_dist) {
            best_dist = dist;
            best = &r;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

namespace detail {

inline float json_float(const nlohmann::json& obj, const char* key, float fallback) {
    return obj.value(key, fallback);
}

inline int json_int(const nlohmann::json& obj, const char* key, int fallback) {
    return obj.value(key, fallback);
}

inline bool json_bool(const nlohmann::json& obj, const char* key, bool fallback) {
    return obj.value(key, fallback);
}

inline std::string json_str(const nlohmann::json& obj, const char* key, const char* fallback) {
    return obj.value(key, std::string(fallback ? fallback : ""));
}

inline std::string base_dir(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return ".";
    return path.substr(0, pos);
}

inline void parse_envelope(const nlohmann::json& obj, float& attack, float& decay,
                           float& sustain, float& release) {
    auto it = obj.find("envelope");
    if (it == obj.end()) return;
    const auto& env = *it;
    attack  = json_float(env, "attack",  attack);
    decay   = json_float(env, "decay",   decay);
    sustain = json_float(env, "sustain", sustain);
    release = json_float(env, "release", release);
}

inline SampleRegion parse_region(const nlohmann::json& obj, const std::string& base,
                                 std::unordered_map<std::string, std::shared_ptr<SampleData>>& cache) {
    SampleRegion r;
    r.root_note      = json_int(obj, "root_note", 60);
    r.lo_note        = json_int(obj, "lo_note", r.root_note);
    r.hi_note        = json_int(obj, "hi_note", r.root_note);
    r.lo_vel         = json_int(obj, "lo_vel", 0);
    r.hi_vel         = json_int(obj, "hi_vel", 127);
    r.volume_db      = json_float(obj, "volume_db", 0.0f);
    r.pan            = json_float(obj, "pan", 0.0f);
    r.tune_cents     = json_int(obj, "tune_cents", 0);
    r.loop_enabled   = json_bool(obj, "loop", false);
    r.loop_start     = static_cast<uint32_t>(json_int(obj, "loop_start", 0));
    r.loop_end       = static_cast<uint32_t>(json_int(obj, "loop_end", 0));
    r.loop_crossfade = static_cast<uint32_t>(json_int(obj, "loop_crossfade", 0));

    std::string sample_path = json_str(obj, "path", nullptr);
    if (!sample_path.empty()) {
        std::string abs_path = base + "/" + sample_path;
        auto it = cache.find(abs_path);
        if (it != cache.end()) {
            r.data = it->second;
        } else {
            auto decoded = decode_wav(abs_path);
            if (decoded) cache[abs_path] = decoded;
            r.data = decoded;
        }
    }

    return r;
}

inline SampleGroup parse_group(const nlohmann::json& obj, const std::string& base,
                               float def_attack, float def_decay,
                               float def_sustain, float def_release,
                               std::unordered_map<std::string, std::shared_ptr<SampleData>>& cache) {
    SampleGroup g;
    g.name     = json_str(obj, "name", "");
    g.keyswitch = json_int(obj, "keyswitch", -1);
    g.attack   = def_attack;
    g.decay    = def_decay;
    g.sustain  = def_sustain;
    g.release  = def_release;
    parse_envelope(obj, g.attack, g.decay, g.sustain, g.release);

    auto sit = obj.find("samples");
    if (sit != obj.end() && sit->is_array()) {
        for (const auto& val : *sit) {
            g.regions.push_back(parse_region(val, base, cache));
        }
    }

    return g;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Load sample bank from JSON
// ---------------------------------------------------------------------------

inline SampleBank* load_sample_bank(const std::string& json_path) {
    nlohmann::json root;
    try {
        std::ifstream ifs(json_path);
        if (!ifs.is_open()) {
            fprintf(stderr, "[vivid-sampler] Failed to read JSON: %s\n", json_path.c_str());
            return nullptr;
        }
        root = nlohmann::json::parse(ifs);
    } catch (const nlohmann::json::parse_error& e) {
        fprintf(stderr, "[vivid-sampler] Failed to read JSON: %s\n", json_path.c_str());
        return nullptr;
    }

    if (!root.is_object()) {
        fprintf(stderr, "[vivid-sampler] Invalid JSON root: %s\n", json_path.c_str());
        return nullptr;
    }

    std::string base = detail::base_dir(json_path);

    auto* bank = new SampleBank();
    bank->name = detail::json_str(root, "name", "untitled");

    // Read top-level envelope defaults
    detail::parse_envelope(root, bank->attack, bank->decay, bank->sustain, bank->release);

    // Deduplicate WAV loading across all groups/regions
    std::unordered_map<std::string, std::shared_ptr<SampleData>> cache;

    auto groups_it = root.find("groups");
    auto samples_it = root.find("samples");

    if (groups_it != root.end() && groups_it->is_array()) {
        // Multi-group format
        for (const auto& val : *groups_it) {
            bank->groups.push_back(detail::parse_group(
                val, base, bank->attack, bank->decay, bank->sustain, bank->release, cache));
        }
    } else if (samples_it != root.end() && samples_it->is_array()) {
        // Single-group format: wrap samples in a default group
        SampleGroup g;
        g.name = "default";
        g.attack  = bank->attack;
        g.decay   = bank->decay;
        g.sustain = bank->sustain;
        g.release = bank->release;

        for (const auto& val : *samples_it) {
            g.regions.push_back(detail::parse_region(val, base, cache));
        }
        bank->groups.push_back(std::move(g));
    }

    fprintf(stderr, "[vivid-sampler] Loaded bank: %s (%zu groups)\n",
            bank->name.c_str(), bank->groups.size());

    return bank;
}

} // namespace vivid_sampler
