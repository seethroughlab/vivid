// Operator-level sweep: load every audio operator dylib, run a representative
// workload at 256 and 1024 frames, report mean ± stddev us/block and the
// fraction of the 256-frame real-time budget each instance burns. Baseline
// table for the "audit-then-attack" optimization plan.

#include "audio/audio_smoke.h"
#include "runtime/operators/operator_loader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifndef VIVID_TEST_PLUGIN_SUFFIX
#define VIVID_TEST_PLUGIN_SUFFIX ".dylib"
#endif

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr int kWarmupBlocks = 32;
constexpr int kMeasureBlocks = 512;
constexpr int kRepeats = 6;
constexpr float kPi = 3.14159265358979323846f;

struct OperatorSpec {
    const char* dylib;   // dylib base name (as registered via add_vivid_operator)
    const char* label;   // human-readable name for the report
    bool allow_silent;   // operator may legitimately produce no audio
};

// Every built audio operator, except mic_input (device-dependent). Order is
// arbitrary — we sort the final table by mean cost.
const OperatorSpec kOperators[] = {
    // Utility / mixing
    {"gain",             "Gain",             false},
    {"mixer",            "Mixer",            false},
    {"stereo_pan_width", "StereoPanWidth",   false},
    {"ring_mod",         "RingMod",          false},

    // Sources (may idle silent if no gate)
    {"oscillator",       "Oscillator",       true},
    {"audio_noise",      "Noise",            false},
    {"fm_synth",         "FmSynth",          true},

    // Samplers (silent without a loaded file)
    {"sampler",          "Sampler",          true},
    {"slicer",           "Slicer",           true},
    {"sp404",            "SP404",            true},

    // Drums (silent without trigger)
    {"drum_kick",        "DrumKick",         true},
    {"drum_snare",       "DrumSnare",        true},
    {"drum_tom",         "DrumTom",          true},
    {"drum_hihat",       "DrumHiHat",        true},
    {"drum_cymbal",      "DrumCymbal",       true},
    {"drum_clap",        "DrumClap",         true},

    // Filters / dynamics
    {"filter",           "Filter",           false},
    {"dual_filter",      "DualFilter",       false},
    {"parametric_eq",    "ParametricEQ",     false},
    {"compressor",       "Compressor",       false},
    {"limiter",          "Limiter",          false},

    // Distortion
    {"distortion",       "Distortion",       false},
    {"bitcrush",         "Bitcrush",         false},

    // Delays / modulation
    {"delay",            "Delay",            false},
    {"ping_pong_delay",  "PingPongDelay",    false},
    {"chorus",           "Chorus",           false},
    {"flanger",          "Flanger",          false},
    {"phaser",           "Phaser",           false},

    // Reverbs
    {"reverb",           "Reverb",           false},
    {"convolution_reverb","ConvolutionReverb",false},

    // Spectral
    {"spectral_freeze",  "SpectralFreeze",   false},
    {"vocoder",          "Vocoder",          false},
    {"granular_synth",   "GranularSynth",    true},

    // Analysis
    {"audio_analysis",   "AudioAnalysis",    true},
};

struct Measurement {
    double mean_us = 0.0;
    double stddev_us = 0.0;
    bool smoke_ok = true;
    const char* smoke_reason = nullptr;
    uint32_t audio_input_count = 0;
    uint32_t audio_output_count = 0;
};

// Per-operator identity-keyed lane state. Operators like Filter/DualFilter
// dereference vivid_lane_state() unconditionally on their first block, so we
// must provide a non-null callback. Reset between cases so state doesn't
// bleed across operators.
struct LaneStateEntry {
    uint32_t lane_id;
    uint32_t byte_size;
    std::vector<uint8_t> data;
};
std::vector<LaneStateEntry> g_lane_states;

void* bench_lane_state(void*, uint32_t lane_id, uint32_t byte_size) {
    for (auto& entry : g_lane_states) {
        if (entry.lane_id == lane_id && entry.byte_size == byte_size)
            return entry.data.data();
    }
    g_lane_states.push_back({lane_id, byte_size, std::vector<uint8_t>(byte_size, 0)});
    return g_lane_states.back().data.data();
}

// Periodic trigger pulse + stereo sine + noise: representative audio-rate
// input that exercises gate-driven operators (samplers, drums) and non-gated
// ones (filters, effects) alike. The pulse lives on input[0] sample 0 of
// every 1024-sample window so envelope-based operators don't idle flat.
void fill_input_signal(float* buf, uint32_t frames_total, uint32_t channels, uint32_t seed) {
    for (uint32_t i = 0; i < frames_total; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const bool trigger = (i % 1024u) == 0u;
        for (uint32_t c = 0; c < channels; ++c) {
            const float phase_shift = 0.25f * static_cast<float>(c);
            float sample = 0.28f * std::sin(2.0f * kPi * 220.0f * t + phase_shift)
                         + 0.12f * std::sin(2.0f * kPi * 780.0f * t + 0.7f * phase_shift);
            // Pink-ish noise via trivial xorshift for repeatability
            uint32_t state = (i * 2654435761u) ^ (c * 2246822519u) ^ seed;
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            sample += 0.05f * (static_cast<float>(state & 0xffffu) / 32768.0f - 1.0f);
            if (trigger && c == 0) sample += 0.9f;
            buf[i * channels + c] = sample;
        }
    }
}

double run_once(vivid::OperatorLoader& loader, uint32_t frames, Measurement& m,
                const OperatorSpec& op) {
    const auto* desc = loader.descriptor();
    if (!desc) return 0.0;

    // Inspect audio ports. Scalar/lane/string inputs get a single-sample
    // zero buffer each; we don't model CV sweeps in the sweep — cost is
    // dominated by audio-rate work.
    struct PortAlloc {
        uint32_t channels = 1;
        uint32_t port_index = 0;
    };
    std::vector<PortAlloc> audio_in;
    std::vector<PortAlloc> audio_out;
    std::vector<PortAlloc> scalar_in;
    std::vector<PortAlloc> lane_in;
    std::vector<PortAlloc> string_in;
    for (uint32_t p = 0; p < desc->port_count; ++p) {
        const auto& port = desc->ports[p];
        const uint32_t ch = std::max<uint32_t>(1u, port.channels);
        if (port.direction == VIVID_PORT_INPUT) {
            if (port.type == VIVID_PORT_AUDIO_BUFFER) audio_in.push_back({ch, p});
            else if (port.type == VIVID_PORT_SCALAR) scalar_in.push_back({ch, p});
            else if (port.type == VIVID_PORT_LANE_ARRAY) lane_in.push_back({ch, p});
            else if (port.type == VIVID_PORT_STRING || port.type == VIVID_PORT_STRING_LANES) string_in.push_back({ch, p});
        } else if (port.direction == VIVID_PORT_OUTPUT) {
            if (port.type == VIVID_PORT_AUDIO_BUFFER) audio_out.push_back({ch, p});
        }
    }

    m.audio_input_count = static_cast<uint32_t>(audio_in.size());
    m.audio_output_count = static_cast<uint32_t>(audio_out.size());

    const int total_blocks = kWarmupBlocks + kMeasureBlocks;
    const size_t total_frames = static_cast<size_t>(total_blocks) * frames;

    // Resolve input port count via descriptor.port_count for scalar+lane
    // slots too — we need a contiguous input_buffers/output_buffers array
    // indexed per descriptor port order (not per-type).
    const size_t total_in_ports = desc->port_count;
    std::vector<float*> input_buf_ptrs(total_in_ports, nullptr);
    std::vector<float*> output_buf_ptrs(total_in_ports, nullptr);

    // But the runtime actually uses one contiguous input_buffers array for
    // INPUT ports in port-order, and one output_buffers array for OUTPUT
    // ports in port-order. Count separately.
    uint32_t in_port_count = 0;
    uint32_t out_port_count = 0;
    for (uint32_t p = 0; p < desc->port_count; ++p) {
        if (desc->ports[p].direction == VIVID_PORT_INPUT) ++in_port_count;
        else if (desc->ports[p].direction == VIVID_PORT_OUTPUT) ++out_port_count;
    }

    std::vector<std::vector<float>> input_storage(in_port_count);
    std::vector<std::vector<float>> output_storage(out_port_count);
    std::vector<float*> input_ptrs(in_port_count, nullptr);
    std::vector<float*> output_ptrs(out_port_count, nullptr);

    // Fill input storage per port.
    uint32_t in_idx = 0;
    uint32_t out_idx = 0;
    uint32_t seed_counter = 1;
    for (uint32_t p = 0; p < desc->port_count; ++p) {
        const auto& port = desc->ports[p];
        const uint32_t ch = std::max<uint32_t>(1u, port.channels);
        if (port.direction == VIVID_PORT_INPUT) {
            if (port.type == VIVID_PORT_AUDIO_BUFFER) {
                // Size = total_frames * channels (planar across blocks).
                input_storage[in_idx].resize(total_frames * ch);
                fill_input_signal(input_storage[in_idx].data(),
                                  static_cast<uint32_t>(total_frames), ch, seed_counter++);
            } else {
                // Scalar, lane_array, string — just a single-sample zero buffer
                // so the operator reads defaults.
                input_storage[in_idx].assign(frames * ch, port.default_value);
            }
            ++in_idx;
        } else if (port.direction == VIVID_PORT_OUTPUT) {
            output_storage[out_idx].assign(frames * ch, 0.0f);
            ++out_idx;
        }
    }

    // Param values = defaults.
    std::vector<float> param_values(desc->param_count, 0.0f);
    std::vector<std::string> file_param_values_storage;
    std::vector<const char*> file_param_ptrs;
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        param_values[i] = desc->params[i].default_value;
        if (desc->params[i].type == VIVID_PARAM_FILE || desc->params[i].type == VIVID_PARAM_TEXT) {
            file_param_values_storage.emplace_back(desc->params[i].default_string ? desc->params[i].default_string : "");
        }
    }
    file_param_ptrs.reserve(file_param_values_storage.size());
    for (const auto& s : file_param_values_storage) file_param_ptrs.push_back(s.c_str());

    // Reset lane-state pool per run so one operator's state never leaks into
    // another.
    g_lane_states.clear();

    // Create instance + fill ctx.
    void* inst = loader.create_instance();
    if (!inst) return 0.0;

    VividAudioContext ctx{};
    ctx.sample_rate = kSampleRate;
    ctx.buffer_size = frames;
    ctx.param_values = param_values.data();
    ctx.file_param_values = file_param_ptrs.empty() ? nullptr : file_param_ptrs.data();
    ctx.file_param_count = static_cast<uint32_t>(file_param_ptrs.size());
    ctx.input_buffers = input_ptrs.empty() ? nullptr : input_ptrs.data();
    ctx.output_buffers = output_ptrs.empty() ? nullptr : output_ptrs.data();
    ctx.lane_count = 1;
    ctx.lane_index = 0;
    ctx.lane_set_id = 1;
    ctx.lane_id = 1;
    ctx.lane_state_fn = bench_lane_state;

    auto set_block_pointers = [&](int block) {
        uint32_t ii = 0;
        uint32_t oi = 0;
        const size_t block_samples = static_cast<size_t>(frames);
        for (uint32_t p = 0; p < desc->port_count; ++p) {
            const auto& port = desc->ports[p];
            const uint32_t ch = std::max<uint32_t>(1u, port.channels);
            if (port.direction == VIVID_PORT_INPUT) {
                if (port.type == VIVID_PORT_AUDIO_BUFFER) {
                    const size_t offset = static_cast<size_t>(block) * block_samples * ch;
                    input_ptrs[ii] = input_storage[ii].data() + offset;
                } else {
                    input_ptrs[ii] = input_storage[ii].data();
                }
                ++ii;
            } else if (port.direction == VIVID_PORT_OUTPUT) {
                output_ptrs[oi] = output_storage[oi].data();
                ++oi;
            }
        }
        ctx.frame = static_cast<uint64_t>(block) * block_samples;
        ctx.time = static_cast<double>(ctx.frame) / static_cast<double>(kSampleRate);
    };

    // Warmup — settles envelopes, delay buffers, filter states.
    for (int block = 0; block < kWarmupBlocks; ++block) {
        set_block_pointers(block);
        loader.process_audio(inst, &ctx);
    }

    // Smoke check on the last warmup block — cheap sanity gate.
    {
        vivid::audio_smoke::Spec spec{};
        spec.allow_silent = op.allow_silent;
        spec.max_peak = 50.0f;   // sweep uses relaxed ceiling; per-op passes can tighten.
        spec.max_dc_ratio = 0.9f;
        if (!output_storage.empty()) {
            const auto r = vivid::audio_smoke::check(
                output_storage[0].data(), frames,
                std::max<uint32_t>(1u, desc->ports[0].channels), spec);
            m.smoke_ok = r.ok;
            m.smoke_reason = r.reason;
        }
    }

    // Measure.
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < kMeasureBlocks; ++block) {
        set_block_pointers(block + kWarmupBlocks);
        loader.process_audio(inst, &ctx);
    }
    const auto end = std::chrono::steady_clock::now();

    loader.destroy_instance(inst);

    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(ns) / 1000.0 / static_cast<double>(kMeasureBlocks);
}

Measurement run_case(const std::filesystem::path& plugin_dir,
                     const OperatorSpec& op, uint32_t frames) {
    Measurement m{};
    vivid::OperatorLoader loader;
    const std::filesystem::path path = plugin_dir / (std::string(op.dylib) + VIVID_TEST_PLUGIN_SUFFIX);
    if (!loader.load(path.c_str())) {
        std::fprintf(stderr, "[skip] failed to load %s\n", path.c_str());
        m.smoke_ok = false;
        m.smoke_reason = "load failed";
        return m;
    }

    std::vector<double> samples;
    samples.reserve(kRepeats);
    double sum = 0.0;
    for (int i = 0; i < kRepeats; ++i) {
        const double us = run_once(loader, frames, m, op);
        samples.push_back(us);
        sum += us;
    }
    m.mean_us = sum / static_cast<double>(kRepeats);
    double variance = 0.0;
    for (double s : samples) {
        const double d = s - m.mean_us;
        variance += d * d;
    }
    m.stddev_us = std::sqrt(variance / static_cast<double>(kRepeats));
    return m;
}

struct Row {
    const char* label;
    const char* dylib;
    uint32_t frames;
    Measurement m;
};

float block_budget_pct(double mean_us, uint32_t frames) {
    const double budget_us = static_cast<double>(frames) / static_cast<double>(kSampleRate) * 1.0e6;
    return static_cast<float>(100.0 * mean_us / budget_us);
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path plugin_dir{"."};
    if (argc > 0 && argv && argv[0]) {
        const std::filesystem::path exe_path(argv[0]);
        const auto parent = exe_path.parent_path();
        if (!parent.empty()) plugin_dir = parent;
    }

    std::printf("Audio operator sweep: sample_rate=%u measure_blocks=%d repeats=%d\n",
                kSampleRate, kMeasureBlocks, kRepeats);

    const uint32_t frames_list[] = {256u, 1024u};
    std::vector<Row> rows;
    rows.reserve(std::size(kOperators) * std::size(frames_list));

    for (const auto& op : kOperators) {
        for (uint32_t frames : frames_list) {
            const auto m = run_case(plugin_dir, op, frames);
            Row row{op.label, op.dylib, frames, m};
            rows.push_back(row);
            std::printf("frames=%-4u case=%-20s mean_us=%9.3f ± %7.3f  budget256=%5.2f%%  smoke=%s%s%s  audio_in=%u audio_out=%u\n",
                        frames,
                        op.label,
                        m.mean_us,
                        m.stddev_us,
                        block_budget_pct(m.mean_us, 256),
                        m.smoke_ok ? "ok" : "FAIL",
                        m.smoke_reason ? " (" : "",
                        m.smoke_reason ? m.smoke_reason : (m.smoke_reason ? ")" : ""),
                        m.audio_input_count,
                        m.audio_output_count);
            std::fflush(stdout);
        }
    }

    // Sorted summary — by 256-frame cost descending.
    std::printf("\n=== Sorted summary (256-frame mean descending) ===\n");
    std::vector<Row*> sorted256;
    for (auto& r : rows) if (r.frames == 256u) sorted256.push_back(&r);
    std::sort(sorted256.begin(), sorted256.end(),
              [](const Row* a, const Row* b) { return a->m.mean_us > b->m.mean_us; });
    for (const Row* r : sorted256) {
        std::printf("%-22s 256f: %8.2f us  (budget %5.2f%%)   1024f: ",
                    r->label,
                    r->m.mean_us,
                    block_budget_pct(r->m.mean_us, 256));
        // find the matching 1024 row
        for (const auto& q : rows) {
            if (q.frames == 1024u && std::strcmp(q.label, r->label) == 0) {
                std::printf("%8.2f us  (budget %5.2f%%)", q.m.mean_us,
                            block_budget_pct(q.m.mean_us, 1024));
                break;
            }
        }
        std::printf("\n");
    }

    return 0;
}
