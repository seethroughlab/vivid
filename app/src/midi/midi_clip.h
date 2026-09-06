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

// Per-note expression (M3): a painted curve per MPE axis. Playback samples these per
// audio block and emits VST3 note-expression events (kTuningTypeID / kBrightnessTypeID /
// per-note pressure). Breakpoints are kept sorted by t; piecewise-linear between them,
// held before the first / after the last. Empty => that axis is flat (zero cost).
enum ExprAxis { AXIS_BEND = 0, AXIS_PRESSURE = 1, AXIS_TIMBRE = 2, AXIS_COUNT = 3 };

struct CurveBp { float t; float v; };   // t = normalized time in the note 0..1; v in axis units

struct ExprCurve {
    std::vector<CurveBp> bp;            // sorted by t; empty = flat
    bool empty() const { return bp.empty(); }
    // Sample at normalized time u (clamped to [0,1]); returns the axis value.
    // Allocation-free — safe on the audio thread. Bend v = semitones; others 0..1.
    float sample(float u) const {
        if (bp.empty()) return 0.f;
        if (u <= bp.front().t) return bp.front().v;
        if (u >= bp.back().t)  return bp.back().v;
        for (size_t i = 1; i < bp.size(); ++i) {
            if (u <= bp[i].t) {
                const CurveBp& a = bp[i - 1]; const CurveBp& b = bp[i];
                const float span = b.t - a.t;
                const float f = span > 1e-9f ? (u - a.t) / span : 0.f;
                return a.v + f * (b.v - a.v);
            }
        }
        return bp.back().v;
    }
};

// start/dur in beats. `expr` carries the optional painted per-axis curves (default empty).
struct ClipNote {
    int pitch; double start; double dur; float vel;
    ExprCurve expr[AXIS_COUNT];
    bool has_expr() const {
        for (int a = 0; a < AXIS_COUNT; ++a) if (!expr[a].empty()) return true;
        return false;
    }
};

// A note-on/off event. `tuning` (semitones) seeds noteOn.tuning at note-on for a
// click-free start when a bend curve is present.
struct NoteEvent { uint32_t sample_offset; bool on; int pitch; float vel; int32_t note_id; float tuning; };

// A per-note expression point emitted mid-note. `value` is in the axis's units
// (bend = semitones; pressure/timbre 0..1); emit_vst3 maps it to the VST3 event.
// `pitch` is carried so the pressure axis can emit a per-note poly-pressure event.
struct ExprEvent { uint32_t sample_offset; int32_t note_id; int pitch; uint8_t axis; float value; };

// --- Clip-level controller automation (P4) ---
//
// Distinct from the per-note ExprCurve above, and deliberately a different type. A per-note curve
// is MPE-shaped: its `t` is normalized 0..1 WITHIN one note, and it plays back as VST3 note
// expression, which only means anything on a plugin that supports it. But a single-channel
// keyboard's mod wheel, sustain pedal and pitch bend are CHANNEL messages — they apply to every
// sounding note at once. Slicing those into per-note curves is a lie that falls apart the moment
// you play a chord. So they live here instead: clip-relative, in beats, one lane per controller.
//
// Keyed in the VST3 Vst::ControllerNumbers space so playback is a single array lookup against the
// plugin's IMidiMapping table: 0..127 = MIDI CC, 128 = channel pressure (kAfterTouch),
// 129 = pitch bend (kPitchBend).
struct CcBp { double t; float v; };   // t = CLIP-LOCAL BEATS (not CurveBp's normalized 0..1); v = 0..1

constexpr int kCcChannelPressure = 128;   // == Steinberg::Vst::kAfterTouch
constexpr int kCcPitchBend       = 129;   // == Steinberg::Vst::kPitchBend
constexpr int kCcSustain         = 64;    // == Steinberg::Vst::kCtrlSustainOnOff
constexpr int kCcCount           = 130;   // == Steinberg::Vst::kCountCtrlNumber
constexpr int kMaxCcLanes        = 16;    // per clip; bounds RT cursor state AND param-queue pressure

struct CcLane {
    uint16_t         cc = 0;         // 0..129, the ControllerNumbers space above
    uint8_t          channel = 0;    // reserved; 0 today (emit_vst3 hard-codes channel 0)
    std::vector<CcBp> bp;            // sorted by t, ascending
    bool empty() const { return bp.empty(); }

    // Sample at clip-local beat `t`, piecewise-linear, held before the first / after the last point.
    // `cur` is CALLER-OWNED monotone cursor state (the scheduler's, never the lane's — the lane is
    // shared with the UI copy). Advancing a cursor makes this O(points crossed) ≈ 1 per block rather
    // than O(n) from the start, which matters because a recorded lane is unbounded in length.
    // Allocation-free: safe on the audio thread.
    float sample(double t, uint32_t& cur) const {
        if (bp.empty()) return 0.f;
        if (t <= bp.front().t) { cur = 0; return bp.front().v; }
        if (t >= bp.back().t)  { cur = static_cast<uint32_t>(bp.size() - 1); return bp.back().v; }
        if (cur >= bp.size()) cur = 0;
        while (cur + 1 < bp.size() && bp[cur + 1].t <= t) ++cur;   // advance
        while (cur > 0 && bp[cur].t > t) --cur;                    // or seek back (loop wrap / scrub)
        const CcBp& a = bp[cur];
        const CcBp& b = bp[cur + 1 < bp.size() ? cur + 1 : cur];
        const double span = b.t - a.t;
        if (span <= 1e-12) return b.v;
        const double f = (t - a.t) / span;
        return static_cast<float>(a.v + f * (b.v - a.v));
    }
};

// A clip-level controller point, emitted by ClipScheduler::emit_cc. Block-granular by construction —
// the VST3 param path reports sampleOffset 0 regardless (vst3_host_common.h SinglePointQueue), so a
// finer resolution would be discarded. The offset is carried anyway: CLAP takes it natively, and it
// costs nothing to keep the seam honest.
struct CcEvent { uint32_t sample_offset; uint16_t cc; uint8_t channel; float value; };

struct MidiClip {
    std::vector<ClipNote> notes;
    // Clip-level controller automation; empty = none (zero cost). Additive on disk: a reader that
    // predates it simply doesn't see the "cc" key.
    std::vector<CcLane> cc;
    double length = 4.0;      // clip length in beats
    // Optimistic-concurrency revision: bumped on every note-content write (session_set_clip). A
    // read-modify-write authoring tool reads this with get_clip and hands it back as `expected_rev`
    // on set_clip; a mismatch means someone else wrote in between, so the stale write is rejected
    // (conflict) instead of silently clobbering. Not persisted — an in-session edit counter only.
    uint64_t rev = 0;
    // Optional in-clip loop region [loop_start, loop_end) in beats. When loop_end >
    // loop_start (a valid sub-range), playback loops within it instead of over [0,length);
    // notes outside the region are silent and a note crossing loop_end is cut. Default
    // (0/0) = loop the whole clip.
    double loop_start = 0.0, loop_end = 0.0;
    // Effective loop region, clamped to the clip. Falls back to the whole clip.
    double loop_lo() const { return (loop_end > loop_start + 1e-9) ? std::max(0.0, loop_start) : 0.0; }
    double loop_hi() const { return (loop_end > loop_start + 1e-9) ? std::min(length, loop_end) : length; }
};

// The playhead is transport beats measured RELATIVE to `launch_beat_` (the beat the clip was
// (re)launched on) so a clip launched mid-playback starts from its own beat 0 — Ableton "launch"
// semantics — instead of inheriting global transport phase. Carried state: the sounding notes
// awaiting their note-off, plus that launch origin.
struct ClipScheduler {
    const MidiClip* clip = nullptr;
    int32_t note_id_seq = 0;
    double  launch_beat_ = 0.0;   // transport beat this clip was launched on (playhead origin)
    // Carries what per-note expression sampling needs: the note's clip-local start +
    // duration, a pointer to its curves, the last value emitted per axis (dedupe), and
    // whether it turned on this block (so its start point emits at the note-on offset,
    // never before it). `src` points into clip->notes — the audio thread MUST flush()+
    // reset() the scheduler whenever clip->notes is re-assigned (edit-apply), or this
    // dangles.  (See vst3_host.cpp edit-apply.)
    struct Active {
        int pitch; int32_t id; double end;   // end in clip-local beats
        double start_local; double dur; const ClipNote* src;
        float last[AXIS_COUNT]; bool started; uint32_t on_off;
    };
    std::vector<Active> active;

    void reset(const MidiClip* c, double launch_beat = 0.0) {
        clip = c; launch_beat_ = launch_beat; note_id_seq = 0; active.clear();
        // Fixed arrays, so this stays allocation-free — reset() runs on the audio thread at a
        // quantized scene switch.
        for (int i = 0; i < kMaxCcLanes; ++i) { cc_cursor_[i] = 0; cc_last_[i] = -1.f; }
        cc_prev_ = -1.0;
    }

    // Call whenever clip->notes is re-assigned (edit-apply): the `src` pointers in
    // `active` point into the old (now-freed) notes vector. Null them so the expression
    // pass skips them; note-offs still fire from the copied pitch/id/end. Held notes get
    // no further expression for their remaining life — a fresh note-on re-resolves src.
    void invalidate_active_src() {
        for (Active& a : active) a.src = nullptr;
        cc_prev_ = -1.0;   // the CC lane vector was re-assigned too — force a re-seek + restate
    }

    // The clip-local playhead for a block, shared by emit() and emit_cc() so the two cannot drift
    // apart. Returns false when the clip/block is degenerate. `p0` is the playhead at block start,
    // `L` the loop period, `lo` the loop-region start.
    bool playhead(double block_start_beats, double delta, double& p0, double& L, double& lo) const {
        if (!clip || clip->length <= 0.0 || delta <= 0.0) return false;
        lo = clip->loop_lo();
        const double hi = clip->loop_hi();
        L = hi - lo;
        if (L <= 0.0) return false;
        double rel = block_start_beats - launch_beat_;
        if (rel < 0.0) rel = 0.0;              // caller (emit) re-anchors; emit_cc must not mutate state
        p0 = lo + std::fmod(rel, L);
        if (p0 < lo) p0 += L;
        return true;
    }

    // Sample every CC lane ONCE per block at the block-start playhead, emitting at sample_offset 0,
    // deduped against the last value sent. One point per lane per block is the right granularity:
    // the VST3 param path collapses everything to the top of the block anyway, and at 512 frames /
    // 48 kHz this is ~94 Hz — comfortably above what a MIDI cable delivers.
    //
    // Kept SEPARATE from emit() rather than widening it: CC sampling needs none of emit()'s note
    // bookkeeping, and emit() has one production call site plus several in tests.
    void emit_cc(double block_start_beats, double delta, uint32_t /*frames*/,
                 std::vector<CcEvent>& out) {
        double p0 = 0.0, L = 0.0, lo = 0.0;
        if (!playhead(block_start_beats, delta, p0, L, lo)) return;
        if (clip->cc.empty()) return;
        const size_t n = std::min(clip->cc.size(), static_cast<size_t>(kMaxCcLanes));

        // A loop wrap (or a scrub backwards) means the controller state must be RESTATED — otherwise
        // a lane that ends high and starts low never sends the low value again and the plugin stays
        // stuck wherever the previous pass left it.
        const bool wrapped = (cc_prev_ < 0.0) || (p0 < cc_prev_ - 1e-9);
        if (wrapped) for (int i = 0; i < kMaxCcLanes; ++i) { cc_cursor_[i] = 0; cc_last_[i] = -1.f; }
        cc_prev_ = p0;

        for (size_t i = 0; i < n; ++i) {
            const CcLane& lane = clip->cc[i];
            if (lane.empty()) continue;
            const float v = lane.sample(p0, cc_cursor_[i]);
            if (!wrapped && v == cc_last_[i]) continue;    // unchanged: don't re-send
            cc_last_[i] = v;
            out.push_back({ 0u, lane.cc, lane.channel, v });
        }
    }

    // Emit note on/off + per-note expression events for a block of `frames` samples that
    // advances the transport by `delta` beats, starting at absolute `block_start_beats`.
    void emit(double block_start_beats, double delta, uint32_t frames,
              std::vector<NoteEvent>& out, std::vector<ExprEvent>& eout) {
        if (!clip || clip->length <= 0.0 || delta <= 0.0) return;
        const double lo = clip->loop_lo(), hi = clip->loop_hi();   // in-clip loop region
        const double L = hi - lo;                                  // loop period
        if (L <= 0.0) return;
        double rel = block_start_beats - launch_beat_;             // beats since this clip launched
        if (rel < 0.0) { launch_beat_ = block_start_beats; rel = 0.0; }  // transport rewound → re-anchor
        double p0 = lo + std::fmod(rel, L);                        // clip-local playhead within [lo, hi)
        if (p0 < lo) p0 += L;
        const double p1 = p0 + delta;  // may exceed hi (block wraps the loop)

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
                out.push_back({ off(m), false, active[i].pitch, 0.f, active[i].id, 0.f });
                active.erase(active.begin() + static_cast<long>(i));
            } else {
                ++i;
            }
        }
        // Note-ons (only notes whose start lies inside the loop region).
        for (const auto& n : clip->notes) {
            if (n.start < lo - 1e-9 || n.start >= hi - 1e-9) continue;
            double m;
            if (in_block(n.start, m)) {
                int32_t id = ++note_id_seq;
                const float tuning = n.expr[AXIS_BEND].empty() ? 0.f : n.expr[AXIS_BEND].sample(0.f);
                const uint32_t so = off(m);
                out.push_back({ so, true, n.pitch, n.vel, id, tuning });
                Active a{}; a.pitch = n.pitch; a.id = id;
                a.end = lo + std::fmod((n.start + n.dur) - lo, L);   // wrap/cut at the loop end
                a.start_local = n.start; a.dur = n.dur; a.src = &n;
                for (int ax = 0; ax < AXIS_COUNT; ++ax) a.last[ax] = 999.f;
                a.started = true; a.on_off = so;
                active.push_back(a);
            }
        }
        // Per-note expression: sample each active note's curves once this block. A note
        // that just turned on emits its start value at the note-on offset; a continuing
        // note emits at the block start (offset 0), deduped against its last value.
        for (Active& a : active) {
            if (!a.src || a.dur <= 0.0 || !a.src->has_expr()) { a.started = false; continue; }
            double elapsed = p0 - a.start_local;
            if (elapsed < 0) elapsed += L;
            const float u = static_cast<float>(std::clamp(elapsed / a.dur, 0.0, 1.0));
            const uint32_t so = a.started ? a.on_off : 0u;
            for (int ax = 0; ax < AXIS_COUNT; ++ax) {
                const ExprCurve& c = a.src->expr[ax];
                if (c.empty()) continue;
                const float v = c.sample(a.started ? 0.f : u);
                if (a.started || v != a.last[ax]) {
                    eout.push_back({ so, a.id, a.pitch, static_cast<uint8_t>(ax), v });
                    a.last[ax] = v;
                }
            }
            a.started = false;
        }
    }

    // Emit note-offs for everything currently sounding (used when swapping clips
    // at a bar boundary so the old clip's notes don't hang).
    void flush(std::vector<NoteEvent>& out) {
        for (const auto& a : active) out.push_back({ 0u, false, a.pitch, 0.f, a.id, 0.f });
        active.clear();
    }

private:
    // Per-lane playback state. AUDIO-THREAD state, so it lives on the scheduler and never on
    // CcLane — the lane itself is shared with the UI's copy of the clip.
    uint32_t cc_cursor_[kMaxCcLanes] = {};
    float    cc_last_[kMaxCcLanes]   = {};   // last value emitted; -1 = nothing yet (values are 0..1)
    double   cc_prev_ = -1.0;                // previous block's playhead; a step backwards = loop wrap
};

}  // namespace vivid::session
