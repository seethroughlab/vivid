// Example package operator (kind: generator): a transport-locked note GENERATOR — it emits one
// note every `every` beats, phase-locked to the metronome, reading no input. This is the reference
// for a PROJECT-LOCAL custom generator: a shared project can carry this .cpp + a manifest entry
// `{ "name": "PulseGen", "kind": "generator", "source": "pulse_gen.cpp" }` and it is compiled on
// load_project, offered in the scene grid's generator list, and draws its own cell thumbnail.
//
// Two things make it a first-class generator instead of a plain instrument:
//   1) `static constexpr VividAudioRole kAudioRole = VIVID_AUDIO_ROLE_GENERATOR;` — the host reads
//      this via the vivid_audio_role() export and lists it as a generator (v14).
//   2) `draw_thumbnail()` — an operator-drawn cell preview, forwarded through the loaded-dylib
//      adapter (v14). Self-contained against operator_api/operator.h; no wgpu.
#include "operator_api/operator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

struct PulseGenOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "PulseGen";
    static constexpr const char* kDisplayName = "Pulse Gen";
    static constexpr const char* kSummary = "Generator: one note every `every` beats, locked to the transport (example package operator).";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "note", "generator", "example" };
    static constexpr VividAudioRole kAudioRole = VIVID_AUDIO_ROLE_GENERATOR;   // <- offered as a scene generator

    vivid::Param<int>   note { "note", 48, 0, 127 };
    vivid::Param<float> every{ "every", 1.0f, 0.25f, 4.0f };   // beats between pulses
    vivid::Param<float> gate { "gate", 0.5f, 0.05f, 1.0f };    // fraction of the step the note is held
    vivid::Param<float> vel  { "velocity", 0.8f, 0.0f, 1.0f };

    long long last_step_ = -1;                 // last fired step (dedup across blocks)
    int       held_pitch_ = -1;                // the currently-sounding note (one voice)
    int32_t   held_id_ = 0;
    double    off_beat_ = -1.0;                // absolute beat when the held note releases
    int32_t   next_id_ = 700000;               // our own note-id namespace

    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&note); o.push_back(&every); o.push_back(&gate); o.push_back(&vel);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        VividPortDescriptor p{};   // a source: SILENT audio output only (like the built-in generators)
        p.name = "output"; p.type = VIVID_PORT_AUDIO_BUFFER; p.direction = VIVID_PORT_OUTPUT;
        p.value_type = VIVID_VALUE_AUDIO; p.multiplicity = VIVID_MULTIPLICITY_SCALAR; p.channels = 2;
        o.push_back(p);
    }

    void process_audio(const VividAudioContext* c) override {
        const uint32_t N = c->buffer_size;
        if (c->output_buffers && c->output_buffers[0])
            for (uint32_t i = 0; i < 2 * N; ++i) c->output_buffers[0][i] = 0.f;   // silent
        VividNoteEvent* out = c->note_out;
        const uint32_t cap = c->note_out_capacity;
        uint32_t on = 0;
        const auto emit = [&](uint32_t off, bool onf, int p, float v, int32_t id) {
            if (!out || on >= cap || p < 0 || p > 127) return;
            out[on++] = VividNoteEvent{ off, static_cast<uint8_t>(onf ? 1 : 0),
                                        static_cast<int16_t>(p), v, id, 0.f };
        };
        const float* pv = c->param_values;
        const int   nt = pv ? static_cast<int>(pv[0]) : note.value;
        const float ev = pv ? pv[1] : every.value;
        const float gt = pv ? pv[2] : gate.value;
        const float vl = pv ? pv[3] : vel.value;
        const double bpm = c->metronome_bpm > 1.f ? c->metronome_bpm : 120.0;
        const double spb = 60.0 * static_cast<double>(c->sample_rate) / bpm;
        const double b0 = c->metronome_beats_elapsed;
        const double b1 = b0 + static_cast<double>(N) / spb;
        const double sb = ev < 0.05f ? 0.05 : ev;                 // beats per pulse
        const auto to_s = [&](double beat) -> uint32_t {
            const double s = (beat - b0) * spb;
            return static_cast<uint32_t>(std::min<double>(N - 1, std::max(0.0, s)));
        };
        if (held_pitch_ >= 0 && off_beat_ >= 0.0 && off_beat_ < b1) {   // release the held note
            emit(to_s(off_beat_), false, held_pitch_, 0.f, held_id_); held_pitch_ = -1; off_beat_ = -1.0;
        }
        long long k = static_cast<long long>(std::ceil(b0 / sb - 1e-9));
        for (; static_cast<double>(k) * sb < b1 - 1e-12; ++k) {
            if (k == last_step_) continue;
            const double sbeat = static_cast<double>(k) * sb;
            if (sbeat < b0 - 1e-12) continue;
            const uint32_t s = to_s(sbeat);
            if (held_pitch_ >= 0) { emit(s, false, held_pitch_, 0.f, held_id_); held_pitch_ = -1; }
            const int32_t id = ++next_id_;
            emit(s, true, nt, vl, id); held_pitch_ = nt; held_id_ = id; off_beat_ = sbeat + gt * sb;
            last_step_ = k;
        }
        if (c->note_out_count) *c->note_out_count = on;
    }

    // Thumbnail: evenly-spaced pulse blocks — how many land in a bar (~ 4 / `every`). ANIMATED: the
    // current pulse (at the transport position ctx->time) flashes bright while its note sounds
    // (within the gate window), stepping across the row as the transport plays.
    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        const VividDrawAPI& d = ctx->draw; if (!d.draw_rect) return;
        const auto pv = [&](int i, float def) { return ctx->param_count > (uint32_t)i ? ctx->param_values[i] : def; };
        const float ev = pv(1, 1.0f);
        const double gt = pv(2, 0.5f);
        const double sb = ev < 0.05f ? 0.05 : ev;
        int per = static_cast<int>(std::lround(4.0f / (ev < 0.05f ? 0.05f : ev)));
        per = per < 1 ? 1 : (per > 16 ? 16 : per);
        const long long curk = (long long)std::floor(ctx->time / sb);
        const int    cur  = (int)(((curk % per) + per) % per);
        const double frac = ctx->time / sb - (double)curk;
        const float w = ctx->surface_width, h = ctx->surface_height;
        const VividColor on = ctx->accent, hot = { 1.f, 1.f, 1.f, 1.f };
        const float bw = std::max(2.0f, w / static_cast<float>(per) * 0.5f);
        for (int i = 0; i < per; ++i) {
            const bool active = (i == cur) && frac < gt;
            const float bh = std::max(2.0f, h * (active ? 0.62f : 0.42f)), y = h * 0.5f - bh * 0.5f;
            const float cx = w * (static_cast<float>(i) + 0.5f) / static_cast<float>(per);
            d.draw_rect(d.opaque, cx - bw * 0.5f, y, bw, bh, active ? hot : on);
        }
    }
};

VIVID_REGISTER(PulseGenOp)
