// Example package operator (kind: instrument): a polyphonic sine synth driven by MIDI notes.
// An INSTRUMENT is a source — it declares NO audio input (only a stereo output) and renders
// audio from the block's note on/off events (ctx->note_events). The host routes a track's MIDI
// to it. Self-contained against operator_api/operator.h; no wgpu.
//
// UI-4a demo op: carries a compound ADSR envelope (attack/decay/sustain/release, the group
// leader flagged VIVID_DISPLAY_ADSR) and a tremolo LFO (waveform enum flagged VIVID_DISPLAY_LFO
// + rate/depth) so the audio-graph inspector can render the ADSR + LFO compound widgets.
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
// One unipolar (0..1) LFO sample for waveform `w` at phase `ph` (0..1).
inline float lfo_sample(int w, double ph) {
    switch (w) {
        case 1: return static_cast<float>(1.0 - std::fabs(2.0 * ph - 1.0));      // triangle
        case 2: return ph < 0.5 ? 1.f : 0.f;                                     // square
        case 3: return static_cast<float>(ph);                                   // saw (rising)
        default: return static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * ph)); // sine
    }
}
}  // namespace

struct SineSynthOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "SineSynth";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;   // ADR-0046
    static constexpr const char* kDisplayName = "Sine Synth";
    static constexpr const char* kSummary = "Polyphonic sine instrument (example instrument package operator).";
    static constexpr std::array<const char*, 3> kKeywords = { "audio", "instrument", "synth" };

    vivid::Param<float> gain{ "gain", 0.5f, 0.0f, 1.0f };
    // ADSR envelope (seconds; sustain is a 0..1 level). The group leader carries the hint; the
    // inspector claims the next 3 params (decay/sustain/release) into one ADSR widget.
    vivid::Param<float> attack { "attack",  0.01f, 0.001f, 2.0f };
    vivid::Param<float> decay  { "decay",   0.10f, 0.001f, 2.0f };
    vivid::Param<float> sustain{ "sustain", 0.70f, 0.0f,   1.0f };
    vivid::Param<float> release{ "release", 0.20f, 0.001f, 3.0f };
    // Tremolo LFO: waveform enum (the hint leader) + rate (Hz) + depth (0..1; 0 = off).
    vivid::Param<int>   lfo_wave{ "lfo_wave", 0, { "sine", "triangle", "square", "saw" } };
    vivid::Param<float> lfo_rate{ "lfo_rate", 5.0f, 0.1f, 20.0f };
    vivid::Param<float> lfo_depth{ "lfo_depth", 0.0f, 0.0f, 1.0f };

    enum Stage { kOff, kAttack, kDecay, kSustain, kRelease };
    struct Voice { bool on = false; int pitch = -1; int32_t id = 0; double phase = 0.0; float vel = 0.f;
                   Stage stage = kOff; float env = 0.f; };
    static constexpr int kV = 16;
    Voice v_[kV];
    double lfo_phase_ = 0.0;   // audio-thread-only tremolo phase (0..1)

    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        vivid::semantic_tag(gain, "amplitude_linear");   // vocabulary-validated at load
        vivid::semantic_intent(gain, "output level");
        gain.display_hint = VIVID_DISPLAY_KNOB;
        attack.display_hint   = VIVID_DISPLAY_ADSR;   // claims attack+decay+sustain+release
        lfo_wave.display_hint = VIVID_DISPLAY_LFO;     // waveform preview + selector
        o.push_back(&gain);
        o.push_back(&attack); o.push_back(&decay); o.push_back(&sustain); o.push_back(&release);
        o.push_back(&lfo_wave); o.push_back(&lfo_rate); o.push_back(&lfo_depth);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(aud_out()); }  // source: output only

    void note_on(int pitch, float vel, int32_t id) {
        for (auto& v : v_) if (!v.on) { v = Voice{ true, pitch, id, 0.0, vel, kAttack, 0.f }; return; }
    }
    void note_off(int32_t id) { for (auto& v : v_) if (v.on && v.id == id) v.stage = kRelease; }

    // Advance one voice's ADSR envelope by a single sample. Times are per-block-constant params;
    // per-sample linear steps derived from the sample rate. Returns the current 0..1 amplitude.
    float advance_env(Voice& v, float a, float d, float s, float rel, float sr) {
        switch (v.stage) {
            case kAttack:
                v.env += 1.f / std::max(a * sr, 1.f);
                if (v.env >= 1.f) { v.env = 1.f; v.stage = kDecay; }
                break;
            case kDecay:
                v.env -= (1.f - s) / std::max(d * sr, 1.f);
                if (v.env <= s) { v.env = s; v.stage = kSustain; }
                break;
            case kSustain: v.env = s; break;
            case kRelease:
                v.env -= s > 0.f ? s / std::max(rel * sr, 1.f) : 1.f / std::max(rel * sr, 1.f);
                if (v.env <= 0.f) { v.env = 0.f; v.stage = kOff; v.on = false; }
                break;
            default: v.env = 0.f; break;
        }
        return v.env;
    }

    // Audio-thread callback (RT-safe). Applies each note event at its sample offset, then sums the
    // active sine voices (each shaped by its ADSR envelope) into the stereo output, with a tremolo
    // LFO on the master gain.
    void process_audio(const VividAudioContext* c) override {
        if (!c->output_buffers) return;
        const uint32_t N = c->buffer_size;
        const float sr = static_cast<float>(c->sample_rate);
        const float* pv = c->param_values;
        const float g   = pv ? pv[0] : gain.value;
        const float a   = pv ? pv[1] : attack.value;
        const float d   = pv ? pv[2] : decay.value;
        const float s   = pv ? pv[3] : sustain.value;
        const float rel = pv ? pv[4] : release.value;
        const int   lw  = pv ? static_cast<int>(pv[5]) : lfo_wave.value;
        const float lr  = pv ? pv[6] : lfo_rate.value;
        const float ld  = pv ? pv[7] : lfo_depth.value;
        const double lfo_step = lr / (sr > 0.f ? sr : 44100.f);
        float* L = c->output_buffers[0];
        float* R = c->output_buffers[0] + N;
        uint32_t ei = 0;
        for (uint32_t i = 0; i < N; ++i) {
            while (ei < c->note_event_count && c->note_events[ei].sample_offset <= i) {
                const VividNoteEvent& e = c->note_events[ei++];
                if (e.on) note_on(e.pitch, e.velocity, e.note_id); else note_off(e.note_id);
            }
            float smp = 0.f;
            for (auto& v : v_) if (v.on) {
                const float amp = advance_env(v, a, d, s, rel, sr);
                const double f = 440.0 * std::pow(2.0, (v.pitch - 69) / 12.0);
                smp += static_cast<float>(std::sin(v.phase)) * v.vel * amp * 0.25f;
                v.phase += 2.0 * kPi * f / sr;
                if (v.phase > 2.0 * kPi) v.phase -= 2.0 * kPi;
            }
            // Tremolo: dip the gain toward (1-depth) as the LFO falls. depth 0 = untouched.
            const float trem = 1.f - ld * (1.f - lfo_sample(lw, lfo_phase_));
            smp *= g * trem;
            lfo_phase_ += lfo_step;
            if (lfo_phase_ >= 1.0) lfo_phase_ -= 1.0;
            L[i] = smp; R[i] = smp;
        }
        while (ei < c->note_event_count) {   // events at/after the block end
            const VividNoteEvent& e = c->note_events[ei++];
            if (e.on) note_on(e.pitch, e.velocity, e.note_id); else note_off(e.note_id);
        }
    }
};

VIVID_REGISTER(SineSynthOp)
