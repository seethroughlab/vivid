// Example package operator (kind: instrument): a polyphonic sine synth driven by MIDI notes.
// An INSTRUMENT is a source — it declares NO audio input (only a stereo output) and renders
// audio from the block's note on/off events (ctx->note_events). The host routes a track's MIDI
// to it. Self-contained against operator_api/operator.h; no wgpu.
#include "operator_api/operator.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
VividPortDescriptor aud_out() {
    VividPortDescriptor p{};
    p.name = "output"; p.type = VIVID_PORT_AUDIO_BUFFER; p.direction = VIVID_PORT_OUTPUT;
    p.value_type = VIVID_VALUE_AUDIO; p.multiplicity = VIVID_MULTIPLICITY_SCALAR; p.channels = 2;
    return p;
}
}  // namespace

struct SineSynthOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "SineSynth";
    static constexpr const char* kDisplayName = "Sine Synth";
    static constexpr const char* kSummary = "Polyphonic sine instrument (example instrument package operator).";
    static constexpr std::array<const char*, 3> kKeywords = { "audio", "instrument", "synth" };

    vivid::Param<float> gain{ "gain", 0.5f, 0.0f, 1.0f };

    struct Voice { bool on = false; int pitch = -1; int32_t id = 0; double phase = 0.0; float vel = 0.f; };
    static constexpr int kV = 16;
    Voice v_[kV];

    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&gain); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(aud_out()); }  // source: output only

    void note_on(int pitch, float vel, int32_t id) {
        for (auto& v : v_) if (!v.on) { v = Voice{ true, pitch, id, 0.0, vel }; return; }
    }
    void note_off(int32_t id) { for (auto& v : v_) if (v.on && v.id == id) v.on = false; }

    // Audio-thread callback (RT-safe). Applies each note event at its sample offset, then sums the
    // active sine voices into the stereo output.
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

VIVID_REGISTER(SineSynthOp)
