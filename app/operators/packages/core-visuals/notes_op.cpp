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

#include <array>
#include <cmath>

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
    }

private:
    VividElement active_[VIVID_MAX_ACTIVE_NOTES];   // owned; kept alive for the frame
    VividElement fired_[VIVID_MAX_NOTE_EVENTS];
    VividSignal  sig_{ active_, 0, fired_, 0 };
};

VIVID_REGISTER(NotesOp)
