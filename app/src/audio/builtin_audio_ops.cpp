#include "audio/builtin_audio_ops.h"
#include "audio/sampler_op.h"        // SamplerLoadable escape hatch
#include "gpu/op_runtime.h"          // OpRegistry / register_op (includes operator_api)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vivid {

namespace {
constexpr double kPi = 3.14159265358979323846;
inline VividPortDescriptor aud_in()  { return { "input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT  }; }
inline VividPortDescriptor aud_out() { return { "output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT }; }
}

// --- Bitcrush: bit-depth + sample-rate reduction (an effect spike) --------------------
struct BitcrushOp : OperatorBase, AudioProcessable {
    static constexpr const char* kDisplayName = "Bitcrush";
    static constexpr const char* kSummary = "Bit-depth + sample-rate reduction (lo-fi crush).";
    static constexpr std::array<const char*, 3> kKeywords = { "audio", "effect", "bitcrush" };

    Param<float> bits{ "bits", 8.f, 1.f, 16.f };
    Param<int>   downsample{ "downsample", 1, 1, 64 };
    Param<float> mix{ "mix", 1.f, 0.f, 1.f };

    float hold_[2] = { 0.f, 0.f };
    int   cnt_[2]  = { 0, 0 };

    void collect_params(std::vector<ParamBase*>& o) override { o.push_back(&bits); o.push_back(&downsample); o.push_back(&mix); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(aud_in()); o.push_back(aud_out()); }

    void process_audio(const VividAudioContext* c) override {
        if (!c->input_buffers || !c->output_buffers) return;
        const uint32_t N = c->buffer_size;
        const uint8_t nch = c->input_channel_counts ? c->input_channel_counts[0] : 1;
        const float b   = c->param_values ? c->param_values[0] : bits.value;
        int         ds  = c->param_values ? static_cast<int>(c->param_values[1] + 0.5f) : static_cast<int>(downsample.value);
        if (ds < 1) ds = 1;
        const float mx  = c->param_values ? c->param_values[2] : mix.value;
        const float levels = std::pow(2.f, b < 1.f ? 1.f : b);
        for (uint8_t ch = 0; ch < nch && ch < 2; ++ch) {
            const float* in  = c->input_buffers[0]  + ch * N;
            float*       out = c->output_buffers[0] + ch * N;
            for (uint32_t i = 0; i < N; ++i) {
                if (--cnt_[ch] < 0) { hold_[ch] = in[i]; cnt_[ch] = ds - 1; }
                const float q = std::round(hold_[ch] * levels) / levels;   // quantize the held sample
                out[i] = in[i] * (1.f - mx) + q * mx;
            }
        }
    }
};

// --- Test Tone: a simple polyphonic sine instrument (an instrument spike) --------------
struct TestToneOp : OperatorBase, AudioProcessable {
    static constexpr const char* kDisplayName = "Test Tone";
    static constexpr const char* kSummary = "Simple sine instrument (native-instrument spike).";
    static constexpr std::array<const char*, 3> kKeywords = { "audio", "instrument", "tone" };

    Param<float> gain{ "gain", 0.5f, 0.f, 1.f };

    struct Voice { bool on = false; int pitch = -1; int32_t id = 0; double phase = 0.0; float vel = 0.f; };
    static constexpr int kV = 16;
    Voice v_[kV];

    void collect_params(std::vector<ParamBase*>& o) override { o.push_back(&gain); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(aud_out()); }

    void note_on(int pitch, float vel, int32_t id) {
        for (auto& v : v_) if (!v.on) { v = Voice{ true, pitch, id, 0.0, vel }; return; }
    }
    void note_off(int32_t id) { for (auto& v : v_) if (v.on && v.id == id) v.on = false; }

    void process_audio(const VividAudioContext* c) override {
        if (!c->output_buffers) return;
        const uint32_t N = c->buffer_size;
        const float sr = static_cast<float>(c->sample_rate);
        const float g  = c->param_values ? c->param_values[0] : gain.value;
        float* L = c->output_buffers[0];
        float* R = c->output_buffers[0] + N;
        uint32_t ei = 0;
        for (uint32_t i = 0; i < N; ++i) {
            while (ei < c->note_event_count && c->note_events[ei].sample_offset <= i) {
                const VividNoteEvent& e = c->note_events[ei++];
                if (e.on) note_on(e.pitch, e.velocity, e.note_id); else note_off(e.note_id);
            }
            float s = 0.f;
            for (auto& v : v_) if (v.on) {
                const double f = 440.0 * std::pow(2.0, (v.pitch - 69) / 12.0);
                s += static_cast<float>(std::sin(v.phase)) * v.vel * 0.25f;
                v.phase += 2.0 * kPi * f / sr;
                if (v.phase > 2.0 * kPi) v.phase -= 2.0 * kPi;
            }
            s *= g;
            L[i] = s; R[i] = s;
        }
        while (ei < c->note_event_count) {   // events at/after the block end
            const VividNoteEvent& e = c->note_events[ei++];
            if (e.on) note_on(e.pitch, e.velocity, e.note_id); else note_off(e.note_id);
        }
    }
};

// --- Sampler: slice-per-note PCM instrument (drum-rack / slicer) -----------------------
// A source instrument that plays one slice of in-memory PCM per note: pitch `base_note`
// triggers slice 0, `base_note+1` slice 1, and so on (the A6 slice→MIDI payoff). PCM +
// slices arrive via the SamplerLoadable escape hatch, not params (see sampler_op.h).
struct SamplerOp : OperatorBase, AudioProcessable, SamplerLoadable {
    static constexpr const char* kDisplayName = "Sampler";
    static constexpr const char* kSummary = "Plays one PCM slice per note (drum-rack / slicer).";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "instrument", "sampler", "slice" };

    Param<int>   base_note{ "base_note", 36, 0, 127 };   // MIDI pitch that maps to slice 0
    Param<float> gain{ "gain", 1.f, 0.f, 2.f };
    Param<int>   gate{ "gate", 0, 0, 1 };                 // 0 = one-shot, 1 = hold-to-gate

    std::vector<float>    pcmL_, pcmR_;                   // planar PCM (device rate)
    std::vector<uint32_t> ss_, se_;                       // slice [start,end) sample regions
    bool                  stereo_ = false;

    struct Voice { bool on = false; int slice = -1; uint32_t pos = 0; float vel = 0.f; int32_t id = 0; };
    static constexpr int kV = 32;
    Voice v_[kV];

    void collect_params(std::vector<ParamBase*>& o) override { o.push_back(&base_note); o.push_back(&gain); o.push_back(&gate); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(aud_out()); }

    void load_pcm(const float* L, const float* R, size_t n, uint32_t /*sr*/,
                  const uint32_t* starts, const uint32_t* ends, int nslices, int base) override {
        pcmL_.assign(L, L + n);
        stereo_ = (R != nullptr);
        pcmR_.assign(stereo_ ? R : L, (stereo_ ? R : L) + n);
        ss_.assign(starts, starts + (nslices > 0 ? nslices : 0));
        se_.assign(ends,   ends   + (nslices > 0 ? nslices : 0));
        base_note.value = static_cast<float>(base);
        for (auto& v : v_) v.on = false;
    }

    void note_on(int pitch, float vel, int32_t id, int base) {
        const int slice = pitch - base;
        if (slice < 0 || slice >= static_cast<int>(ss_.size())) return;
        for (auto& v : v_) if (!v.on) { v = Voice{ true, slice, 0u, vel, id }; return; }
    }
    void note_off(int32_t id, bool gated) {
        if (!gated) return;                                // one-shot voices ring out
        for (auto& v : v_) if (v.on && v.id == id) v.on = false;
    }

    void process_audio(const VividAudioContext* c) override {
        if (!c->output_buffers) return;
        const uint32_t N = c->buffer_size;
        const int   base  = c->param_values ? static_cast<int>(std::lround(c->param_values[0])) : base_note.int_value();
        const float g     = c->param_values ? c->param_values[1] : gain.value;
        const bool  gated = (c->param_values ? c->param_values[2] : gate.value) > 0.5f;
        float* L = c->output_buffers[0];
        float* R = c->output_buffers[0] + N;
        uint32_t ei = 0;
        for (uint32_t i = 0; i < N; ++i) {
            while (ei < c->note_event_count && c->note_events[ei].sample_offset <= i) {
                const VividNoteEvent& e = c->note_events[ei++];
                if (e.on) note_on(e.pitch, e.velocity, e.note_id, base); else note_off(e.note_id, gated);
            }
            float sl = 0.f, sr = 0.f;
            for (auto& v : v_) if (v.on) {
                const uint32_t rp = ss_[v.slice] + v.pos;
                if (rp >= se_[v.slice] || rp >= pcmL_.size()) { v.on = false; continue; }
                sl += pcmL_[rp] * v.vel;
                sr += pcmR_[rp] * v.vel;
                ++v.pos;
            }
            L[i] = sl * g;
            R[i] = sr * g;
        }
        while (ei < c->note_event_count) {                 // events at/after the block end
            const VividNoteEvent& e = c->note_events[ei++];
            if (e.on) note_on(e.pitch, e.velocity, e.note_id, base); else note_off(e.note_id, gated);
        }
    }
};

void register_glitch_ops(OpRegistry& reg);   // audio/glitch/glitch_ops.cpp

void register_builtin_audio_ops(OpRegistry& reg) {
    register_op<BitcrushOp>(reg, "Bitcrush");
    register_op<TestToneOp>(reg, "TestTone");
    register_op<SamplerOp>(reg, "Sampler");
    register_glitch_ops(reg);
}

}  // namespace vivid
