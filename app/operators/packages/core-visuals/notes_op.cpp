// Core visual package operator: Notes — the ADAPTER that turns a track's MIDI into Vivid's generic
// reactive signal. It reads the engine's note buses by track STABLE id (its `track` param) and emits ONE
// VividSignal on a custom-ref output: `active` = currently-held notes (a persistent set → chords bloom,
// arps trail), `fired` = note-ons THIS frame (discrete → re-struck notes fire again). Downstream draw
// ops (Instancer, Emitter, …) read whichever stream they want and never refer to "notes" — so the same
// consumers work off a future Beat/onset source. pitch → pos (0..1 over MIDI), velocity → amp; active
// carries pitch as id (stable while held), fired carries the per-voice note_id (a re-strike is distinct).
//
// A source (no inputs). Its output is a VividSignal custom-ref, NOT a texture — the render is downstream.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/element_geom.h"   // VividSignal, VividElement, publish_signal
#include "operator_api/note_bus.h"       // vivid_track_active_notes (held set)
#include "operator_api/note_events.h"    // vivid_track_note_events (on/off events)
#include "operator_api/lane_thumb.h"     // 2D CPU raster into the node's output texture (same idiom as the lane ops)

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

struct NotesOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Notes";
    static constexpr const char* kDisplayName = "Notes";
    static constexpr const char* kSummary = "A track's live MIDI as a generic signal (held notes + note-on fires) — drives Instancer, Emitter, or any consumer through an edge.";
    static constexpr std::array<const char*, 3> kKeywords = {"notes", "midi", "source"};

    vivid::Param<float> track{"track", 0.f, 0.f, 127.f};   // which track's notes, by STABLE id

    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&track); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(VIVID_CUSTOM_REF_PORT("signal", VIVID_PORT_OUTPUT, VividSignal));
    }

    void draw_thumbnail(const VividThumbnailContext*) override {}   // the live preview is drawn in process_gpu

    void process_gpu(const VividGpuContext* c) override {
        const float* p = c->param_values;
        const int track_id = static_cast<int>(std::lround(p ? p[0] : track.value));   // stable id (the bus searches)

        // active = held notes (membership); id = pitch (stable identity while held).
        VividActiveNote held[VIVID_MAX_ACTIVE_NOTES];
        const uint32_t na = vivid_track_active_notes(track_id, held, VIVID_MAX_ACTIVE_NOTES);
        for (uint32_t i = 0; i < na; ++i)
            active_[i] = { held[i].pitch / 127.f, held[i].velocity, held[i].pitch };

        // fired = note-ONs this frame; id = per-voice note_id (a re-strike of the same pitch is distinct).
        VividNoteHit ev[VIVID_MAX_NOTE_EVENTS];
        const uint32_t ne = vivid_track_note_events(track_id, ev, VIVID_MAX_NOTE_EVENTS);
        uint32_t nf = 0;
        for (uint32_t i = 0; i < ne; ++i)
            if (ev[i].kind == 1) fired_[nf++] = { ev[i].pitch / 127.f, ev[i].velocity, ev[i].note_id };

        sig_ = { active_, na, fired_, nf };
        vivid::elements::publish_signal(c, 0, &sig_);   // downstream reads it this frame

        draw_signal_thumb(c, na, nf);
    }

    ~NotesOp() override { vivid::lanethumb::destroy(thumb_); }

private:
    // A live picture of the signal on the node card: each ACTIVE element is a slim bar standing at its
    // pos (0..1 → left→right), height/brightness from amp; each FIRED element is a bright flash marker
    // over its pos. Generic — it plots pos/amp/fired, not "notes". Same raster idiom as the lane ops.
    void draw_signal_thumb(const VividGpuContext* c, uint32_t na, uint32_t nf) {
        std::vector<vivid::lanethumb::Vtx> v;
        v.reserve((na + nf) * 6 + 6);
        // pos → a warm-to-cool hue ramp so different pitches read as different colours.
        auto hue = [](float t, float dim, float out[3]) {
            out[0] = (0.30f + 0.70f * t) * dim;
            out[1] = (0.45f + 0.35f * std::sin(3.14159265f * std::clamp(t, 0.f, 1.f))) * dim;
            out[2] = (1.00f - 0.65f * t) * dim;
        };
        for (uint32_t i = 0; i < na; ++i) {
            const float pos = std::clamp(active_[i].pos, 0.f, 1.f);
            const float amp = std::clamp(active_[i].amp, 0.f, 1.f);
            const float x = -1.f + 2.f * pos, bw = 0.045f;
            float col[3]; hue(pos, 0.55f + 0.45f * amp, col);
            const float h = 0.28f + 1.30f * amp;
            vivid::lanethumb::quad(v, x - bw, -0.9f, x + bw, -0.9f + h, col);
        }
        for (uint32_t i = 0; i < nf; ++i) {
            const float x = -1.f + 2.f * std::clamp(fired_[i].pos, 0.f, 1.f), bw = 0.06f;
            const float flash[3] = { 1.0f, 0.97f, 0.85f };
            vivid::lanethumb::quad(v, x - bw, -0.92f, x + bw, 0.92f, flash);
        }
        if (v.empty()) {   // idle & silent: a faint centre tick so the card reads as alive, not broken
            const float col[3] = { 0.13f, 0.15f, 0.20f };
            vivid::lanethumb::quad(v, -0.02f, -0.9f, 0.02f, -0.58f, col);
        }
        vivid::lanethumb::draw(c, thumb_, v);
    }

    VividElement active_[VIVID_MAX_ACTIVE_NOTES];   // owned; kept alive for the frame
    VividElement fired_[VIVID_MAX_NOTE_EVENTS];
    VividSignal  sig_{ active_, 0, fired_, 0 };
    vivid::lanethumb::State thumb_{};               // node-thumbnail raster state
};

VIVID_REGISTER(NotesOp)
