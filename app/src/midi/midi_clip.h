#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

// Minimal MIDI clip + transport-locked, sample-accurate scheduler. Captures the
// proven approach from classic's midi_clip_core.h (loop playhead = fmod against
// the master beat; note-on at a sample offset within the block; deferred
// note-off tracked per active note) without the operator-framework baggage.
namespace vivid::session {

struct ClipNote { int pitch; double start; double dur; float vel; };  // start/dur in beats

struct NoteEvent { uint32_t sample_offset; bool on; int pitch; float vel; int32_t note_id; };

struct MidiClip {
    std::vector<ClipNote> notes;
    double length = 4.0;  // loop length in beats
};

// Stateless w.r.t. the playhead (it reads absolute transport beats each block);
// the only carried state is the set of sounding notes awaiting their note-off.
struct ClipScheduler {
    const MidiClip* clip = nullptr;
    int32_t note_id_seq = 0;
    struct Active { int pitch; int32_t id; double end; };  // end in clip-local beats
    std::vector<Active> active;

    void reset(const MidiClip* c) { clip = c; note_id_seq = 0; active.clear(); }

    // Emit note on/off events for a block of `frames` samples that advances the
    // transport by `delta` beats, starting at absolute `block_start_beats`.
    void emit(double block_start_beats, double delta, uint32_t frames,
              std::vector<NoteEvent>& out) {
        if (!clip || clip->length <= 0.0 || delta <= 0.0) return;
        const double L = clip->length;
        double p0 = std::fmod(block_start_beats, L);
        if (p0 < 0) p0 += L;
        const double p1 = p0 + delta;  // may exceed L (block wraps the loop)

        auto off = [&](double t) -> uint32_t {
            double f = (t - p0) / delta * frames;
            return static_cast<uint32_t>(std::clamp(f, 0.0, static_cast<double>(frames - 1)));
        };
        auto in_block = [&](double x, double& mapped) -> bool {
            double e = (x < p0) ? x + L : x;  // unwrap to [p0, p0+L)
            mapped = e;
            return e >= p0 && e < p1;
        };

        // Note-offs first so a re-triggered pitch ends cleanly before its next on.
        for (size_t i = 0; i < active.size();) {
            double m;
            if (in_block(active[i].end, m)) {
                out.push_back({ off(m), false, active[i].pitch, 0.f, active[i].id });
                active.erase(active.begin() + static_cast<long>(i));
            } else {
                ++i;
            }
        }
        // Note-ons.
        for (const auto& n : clip->notes) {
            double m;
            if (in_block(n.start, m)) {
                int32_t id = ++note_id_seq;
                out.push_back({ off(m), true, n.pitch, n.vel, id });
                active.push_back({ n.pitch, id, std::fmod(n.start + n.dur, L) });
            }
        }
    }

    // Emit note-offs for everything currently sounding (used when swapping clips
    // at a bar boundary so the old clip's notes don't hang).
    void flush(std::vector<NoteEvent>& out) {
        for (const auto& a : active) out.push_back({ 0u, false, a.pitch, 0.f, a.id });
        active.clear();
    }
};

}  // namespace vivid::session
