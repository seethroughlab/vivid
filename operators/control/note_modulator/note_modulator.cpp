// NoteModulator — generates time-varying per-note expression (PRESSURE,
// TIMBRE, PITCH_BEND) from internal LFO shapers, keyed by note_id. Slots
// between any note source and any synth that consumes the native note
// buffer to give every active voice its own independent expression motion
// without the synth knowing anything about it.
//
// Topology:
//   ChordProgression/notes_out → NoteModulator/notes_in → Synth/notes_in
//
// Compose semantics: the modulator's contribution is *added* to whatever
// expression the upstream source already supplied (so an MPE controller's
// hand-played pressure stacks with our LFO). A per-channel `*_mode` enum
// flips a channel to `replace` when override semantics are wanted.
//
// Per-note independence: each active note gets its own phase per channel,
// randomized at note-on by hashing the note_id. Simultaneous chord notes
// therefore breathe at slightly different phases instead of in lockstep.

#include "operator_api/operator.h"
#include "operator_api/metronome_sync.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "operator_api/voice_table.h"
#include "control/audio_scalar_utils.h"
#include "shared/sequencer/note_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

// Modulation channels we generate. Order matches the param-collection
// order so editors group the params logically.
enum ModChannel : int {
    kChannelTimbre = 0,
    kChannelPressure = 1,
    kChannelPitchBend = 2,
    kChannelCount = 3,
};

inline uint32_t hash_note_id(uint64_t note_id, int channel) {
    // Splittable hash: mix note_id with a per-channel salt so the three
    // channels of the same note don't all start at the same phase.
    uint64_t x = note_id ^ (0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(channel + 1));
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<uint32_t>(x);
}

inline float hash_to_unit(uint32_t h) {
    return static_cast<float>(h) / 4294967296.0f;
}

inline uint32_t lcg_step(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state;
}

inline float lcg_unit_signed(uint32_t& state) {
    return static_cast<float>(static_cast<int32_t>(lcg_step(state))) / 2147483648.0f;
}

inline float catmull_rom(float p0, float p1, float p2, float p3, float t) {
    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
}

// Per-note-per-channel modulator state. One entry per (slot, channel).
struct ChannelState {
    // Free-mode phase accumulator. In free mode this advances by
    // rate_hz × dt_per_block each frame; in metronome / external modes
    // it's unused (phase comes from the global transport instead).
    double phase_accum = 0.0;
    // Per-note hash-seeded phase offset, applied on top of whichever
    // phase source is active. With phase_random > 0, simultaneous notes
    // get distinct offsets even when sharing a global metronome phase.
    float phase_random_offset = 0.0f;
    // Composite phase fed to eval_waveform — kept for prev_phase wrap
    // detection on sample_hold / smooth_random.
    float phase = 0.0f;             // [0, 1)
    float prev_phase = 0.0f;        // for wrap detection
    float attack_progress = 0.0f;   // [0, 1] — depth fade-in after note-on
    uint32_t rng = 12345u;          // S&H / smooth_random RNG
    float sh_value = 0.0f;
    float sh_prev = 0.0f;
    float sh_next = 0.0f;
    float sh_prev2 = 0.0f;
};

// Evaluate a single waveform sample for a phase in [0, 1) and return value
// in the natural waveform range (sine/triangle/saw: -1..1, square: ±1,
// sample_hold/smooth_random: -1..1). Polarity / amount / attack scaling
// are applied by the caller.
inline float eval_waveform(int wf, ChannelState& st, float phase) {
    bool wrapped = phase < st.prev_phase - 0.5f;
    st.prev_phase = phase;

    switch (wf) {
        case 0: return std::sin(phase * 2.0f * static_cast<float>(M_PI));   // sine
        case 1: return 4.0f * (phase < 0.5f ? phase : (1.0f - phase)) - 1.0f; // triangle
        case 2: return 2.0f * phase - 1.0f;                                  // saw
        case 3: return phase < 0.5f ? 1.0f : -1.0f;                          // square
        case 4: {                                                            // sample_hold
            if (wrapped) st.sh_value = lcg_unit_signed(st.rng);
            return st.sh_value;
        }
        case 5: {                                                            // smooth_random
            if (wrapped) {
                st.sh_prev2 = st.sh_prev;
                st.sh_prev  = st.sh_next;
                st.sh_next  = st.sh_value;
                st.sh_value = lcg_unit_signed(st.rng);
            }
            return catmull_rom(st.sh_prev2, st.sh_prev, st.sh_next, st.sh_value, phase);
        }
        default: return 0.0f;
    }
}

}  // namespace

/**
 * @brief Per-note time-varying expression generator.
 *
 * Sits between a note source and a synth, consumes the native note buffer,
 * and emits PRESSURE / TIMBRE / PITCH_BEND events per-active-note from
 * internal LFO shapers — one independent shaper per (note, channel). Each
 * note's starting phase is randomized from its note_id so simultaneous
 * chord notes breathe out of phase with each other.
 *
 * Compose mode (default): the modulator's contribution is added to the
 * upstream source's expression. Hand-played MPE pressure stacks with the
 * LFO. Per-channel `*_mode` toggles to `replace` when override is wanted.
 *
 * @input notes_in Native note stream (VividNoteBuffer).
 * @output notes_out Pass-through of notes_in plus injected expression
 *         events for each active note on each enabled channel.
 *
 * @recipe note_source/notes_out -> NoteModulator/notes_in
 * @recipe NoteModulator/notes_out -> Synth/notes_in
 *
 * @family note_processor
 * @best_used_with WavetableLayer, WavetableOsc, Sampler, AnalogOsc
 * @common_companions ChordProgression, MidiInput, Tracker, Arpeggiator
 */
struct NoteModulator : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "NoteModulator";
    static constexpr bool kTimeDependent = true;
    static constexpr int kMaxVoices = 16;

    // ── Per-channel params ──────────────────────────────────────────
    // Defaults: amount=0 means the channel is OFF (no events emitted,
    // zero CPU). The user opts in by raising amount on the channel they
    // want.

    // Timbre channel — bipolar by default (timbre swings around the
    // upstream value), 0..1 amount range.
    vivid::Param<float> timbre_amount       {"timbre_amount",       0.0f, 0.0f, 1.0f};
    vivid::Param<int>   timbre_clock_mode    {"timbre_clock_mode",    vivid::kClockModeInternal,
                                             vivid::clock_mode_full_labels()};
    vivid::Param<float> timbre_rate_hz      {"timbre_rate_hz",      0.5f, 0.01f, 20.0f};
    vivid::Param<int>   timbre_sync_division{"timbre_sync_division", 2,
                                             vivid::metronome_division_labels()};
    vivid::Param<int>   timbre_waveform     {"timbre_waveform",     0,
        {"sine", "triangle", "saw", "square", "sample_hold", "smooth_random"}};
    vivid::Param<float> timbre_phase_offset {"timbre_phase_offset", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> timbre_phase_random {"timbre_phase_random", 0.3f, 0.0f, 1.0f};
    vivid::Param<int>   timbre_polarity     {"timbre_polarity",     0,  {"bipolar", "unipolar"}};
    vivid::Param<float> timbre_attack_ms    {"timbre_attack_ms",    5.0f, 0.0f, 1000.0f};
    vivid::Param<int>   timbre_mode         {"timbre_mode",         0,  {"add", "replace"}};

    // Pressure channel — unipolar default (pressure is naturally 0..1).
    vivid::Param<float> pressure_amount       {"pressure_amount",       0.0f, 0.0f, 1.0f};
    vivid::Param<int>   pressure_clock_mode    {"pressure_clock_mode",    vivid::kClockModeInternal,
                                               vivid::clock_mode_full_labels()};
    vivid::Param<float> pressure_rate_hz      {"pressure_rate_hz",      0.5f, 0.01f, 20.0f};
    vivid::Param<int>   pressure_sync_division{"pressure_sync_division", 2,
                                               vivid::metronome_division_labels()};
    vivid::Param<int>   pressure_waveform     {"pressure_waveform",     0,
        {"sine", "triangle", "saw", "square", "sample_hold", "smooth_random"}};
    vivid::Param<float> pressure_phase_offset {"pressure_phase_offset", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> pressure_phase_random {"pressure_phase_random", 0.3f, 0.0f, 1.0f};
    vivid::Param<int>   pressure_polarity     {"pressure_polarity",     1,  {"bipolar", "unipolar"}};
    vivid::Param<float> pressure_attack_ms    {"pressure_attack_ms",    5.0f, 0.0f, 1000.0f};
    vivid::Param<int>   pressure_mode         {"pressure_mode",         0,  {"add", "replace"}};

    // Pitch bend channel — bipolar default, depth in semitones (0..12).
    vivid::Param<float> pitch_bend_amount        {"pitch_bend_amount",        0.0f, 0.0f, 12.0f};
    vivid::Param<int>   pitch_bend_clock_mode     {"pitch_bend_clock_mode",     vivid::kClockModeInternal,
                                                  vivid::clock_mode_full_labels()};
    vivid::Param<float> pitch_bend_rate_hz       {"pitch_bend_rate_hz",       5.0f, 0.01f, 20.0f};
    vivid::Param<int>   pitch_bend_sync_division {"pitch_bend_sync_division", 2,
                                                  vivid::metronome_division_labels()};
    vivid::Param<int>   pitch_bend_waveform      {"pitch_bend_waveform",      0,
        {"sine", "triangle", "saw", "square", "sample_hold", "smooth_random"}};
    vivid::Param<float> pitch_bend_phase_offset  {"pitch_bend_phase_offset",  0.0f, 0.0f, 1.0f};
    vivid::Param<float> pitch_bend_phase_random  {"pitch_bend_phase_random",  0.3f, 0.0f, 1.0f};
    vivid::Param<int>   pitch_bend_polarity      {"pitch_bend_polarity",      0,  {"bipolar", "unipolar"}};
    vivid::Param<float> pitch_bend_attack_ms     {"pitch_bend_attack_ms",     5.0f, 0.0f, 1000.0f};
    vivid::Param<int>   pitch_bend_mode          {"pitch_bend_mode",          0,  {"add", "replace"}};

    // ── Internal state ──────────────────────────────────────────────
    vivid::VoiceTable<kMaxVoices> alloc_;
    uint64_t frame_counter_ = 0;
    ChannelState slot_state_[kMaxVoices][kChannelCount] = {};
    bool slot_inited_[kMaxVoices][kChannelCount] = {};
    VividNoteBuffer out_buf_{};

    NoteModulator() {
        param_group(timbre_amount,        "Timbre");
        param_group(timbre_clock_mode,     "Timbre");
        param_group(timbre_rate_hz,       "Timbre");
        param_group(timbre_sync_division, "Timbre");
        param_group(timbre_waveform,      "Timbre");
        param_group(timbre_phase_offset,  "Timbre");
        param_group(timbre_phase_random,  "Timbre");
        param_group(timbre_polarity,      "Timbre");
        param_group(timbre_attack_ms,     "Timbre");
        param_group(timbre_mode,          "Timbre");

        param_group(pressure_amount,        "Pressure");
        param_group(pressure_clock_mode,     "Pressure");
        param_group(pressure_rate_hz,       "Pressure");
        param_group(pressure_sync_division, "Pressure");
        param_group(pressure_waveform,      "Pressure");
        param_group(pressure_phase_offset,  "Pressure");
        param_group(pressure_phase_random,  "Pressure");
        param_group(pressure_polarity,      "Pressure");
        param_group(pressure_attack_ms,     "Pressure");
        param_group(pressure_mode,          "Pressure");

        param_group(pitch_bend_amount,        "PitchBend");
        param_group(pitch_bend_clock_mode,     "PitchBend");
        param_group(pitch_bend_rate_hz,       "PitchBend");
        param_group(pitch_bend_sync_division, "PitchBend");
        param_group(pitch_bend_waveform,      "PitchBend");
        param_group(pitch_bend_phase_offset,  "PitchBend");
        param_group(pitch_bend_phase_random,  "PitchBend");
        param_group(pitch_bend_polarity,      "PitchBend");
        param_group(pitch_bend_attack_ms,     "PitchBend");
        param_group(pitch_bend_mode,          "PitchBend");

        vivid::semantic_tag(timbre_rate_hz,     "frequency_hz");
        vivid::semantic_unit(timbre_rate_hz,    "Hz");
        vivid::semantic_tag(pressure_rate_hz,   "frequency_hz");
        vivid::semantic_unit(pressure_rate_hz,  "Hz");
        vivid::semantic_tag(pitch_bend_rate_hz, "frequency_hz");
        vivid::semantic_unit(pitch_bend_rate_hz, "Hz");

        vivid::semantic_unit(pitch_bend_amount, "semitones");

        vivid::description(timbre_amount,
            "Depth of the timbre LFO. 0 = channel off (no events emitted).");
        vivid::description(pressure_amount,
            "Depth of the pressure LFO. 0 = channel off.");
        vivid::description(pitch_bend_amount,
            "Depth of the pitch bend LFO in semitones. 0 = channel off.");
        vivid::description(timbre_phase_random,
            "0 = all simultaneous notes share LFO phase. 1 = fully decorrelated per note_id.");
        vivid::description(timbre_mode,
            "add: output = upstream + modulator. replace: modulator overrides upstream.");
        vivid::description(timbre_clock_mode,
            "free = internal Hz, external = drive from beat_phase input, metronome = sync to graph transport.");
        vivid::description(timbre_sync_division,
            "Musical note length used when clock_mode = metronome.");
        vivid::description(pressure_clock_mode,
            "free = internal Hz, external = drive from beat_phase input, metronome = sync to graph transport.");
        vivid::description(pitch_bend_clock_mode,
            "free = internal Hz, external = drive from beat_phase input, metronome = sync to graph transport.");

        // Hide rate_hz when locked to metronome; show sync_division only then.
        vivid::visible_when_ne(timbre_rate_hz,        timbre_clock_mode,     vivid::kClockModeMetronome);
        vivid::visible_when_eq(timbre_sync_division,  timbre_clock_mode,     vivid::kClockModeMetronome);
        vivid::visible_when_ne(pressure_rate_hz,      pressure_clock_mode,   vivid::kClockModeMetronome);
        vivid::visible_when_eq(pressure_sync_division, pressure_clock_mode,  vivid::kClockModeMetronome);
        vivid::visible_when_ne(pitch_bend_rate_hz,    pitch_bend_clock_mode, vivid::kClockModeMetronome);
        vivid::visible_when_eq(pitch_bend_sync_division, pitch_bend_clock_mode, vivid::kClockModeMetronome);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&timbre_amount);        out.push_back(&timbre_clock_mode);
        out.push_back(&timbre_rate_hz);       out.push_back(&timbre_sync_division);
        out.push_back(&timbre_waveform);      out.push_back(&timbre_phase_offset);
        out.push_back(&timbre_phase_random);  out.push_back(&timbre_polarity);
        out.push_back(&timbre_attack_ms);     out.push_back(&timbre_mode);

        out.push_back(&pressure_amount);        out.push_back(&pressure_clock_mode);
        out.push_back(&pressure_rate_hz);       out.push_back(&pressure_sync_division);
        out.push_back(&pressure_waveform);      out.push_back(&pressure_phase_offset);
        out.push_back(&pressure_phase_random);  out.push_back(&pressure_polarity);
        out.push_back(&pressure_attack_ms);     out.push_back(&pressure_mode);

        out.push_back(&pitch_bend_amount);        out.push_back(&pitch_bend_clock_mode);
        out.push_back(&pitch_bend_rate_hz);       out.push_back(&pitch_bend_sync_division);
        out.push_back(&pitch_bend_waveform);      out.push_back(&pitch_bend_phase_offset);
        out.push_back(&pitch_bend_phase_random);  out.push_back(&pitch_bend_polarity);
        out.push_back(&pitch_bend_attack_ms);     out.push_back(&pitch_bend_mode);
    }

    // Port indices: notes_in=0 (custom_ref), beat_phase=1 (scalar) for inputs.
    // Output: notes_out=0 (custom_ref). beat_phase is optional — used only
    // when any channel has clock_mode = external.
    static constexpr uint32_t kBeatPhasePortIdx = 1;

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in",  VIVID_PORT_INPUT,  VividNoteBuffer)); // [0]
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "beat_phase"});                   // [1]
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer));
    }

    // Reads the per-channel params for one of the three channels. Returns
    // by-value (small struct) for the inner-loop call.
    struct ChannelParamSnapshot {
        float amount;
        int   clock_mode;
        float rate_hz;
        int   sync_division;
        int   waveform;
        float phase_offset;
        float phase_random;
        int   polarity;
        float attack_ms;
        int   mode;
    };

    ChannelParamSnapshot snapshot(int c) {
        switch (c) {
            case kChannelTimbre:
                return {timbre_amount.value,
                        timbre_clock_mode.int_value(), timbre_rate_hz.value,
                        timbre_sync_division.int_value(), timbre_waveform.int_value(),
                        timbre_phase_offset.value, timbre_phase_random.value,
                        timbre_polarity.int_value(), timbre_attack_ms.value, timbre_mode.int_value()};
            case kChannelPressure:
                return {pressure_amount.value,
                        pressure_clock_mode.int_value(), pressure_rate_hz.value,
                        pressure_sync_division.int_value(), pressure_waveform.int_value(),
                        pressure_phase_offset.value, pressure_phase_random.value,
                        pressure_polarity.int_value(), pressure_attack_ms.value, pressure_mode.int_value()};
            case kChannelPitchBend:
                return {pitch_bend_amount.value,
                        pitch_bend_clock_mode.int_value(), pitch_bend_rate_hz.value,
                        pitch_bend_sync_division.int_value(), pitch_bend_waveform.int_value(),
                        pitch_bend_phase_offset.value, pitch_bend_phase_random.value,
                        pitch_bend_polarity.int_value(), pitch_bend_attack_ms.value, pitch_bend_mode.int_value()};
            default:
                return {0.0f, vivid::kClockModeInternal, 0.5f, 2, 0, 0.0f, 0.0f, 0, 0.0f, 0};
        }
    }

    void process_audio(const VividAudioContext* ctx) override {
        // ── Pass 1: forward upstream events into out_buf_ verbatim, and
        // drive our internal allocator so slot fields reflect upstream's
        // intent (PRESSURE / TIMBRE / PITCH_BEND values land on the slot
        // via process_note_buffer's apply_* helpers).
        const VividNoteBuffer* notes_in = nullptr;
        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0])
            notes_in = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);

        out_buf_.count = 0;
        if (notes_in) {
            uint32_t n = notes_in->count;
            if (n > VIVID_NOTE_BUFFER_CAPACITY) n = VIVID_NOTE_BUFFER_CAPACITY;
            for (uint32_t i = 0; i < n; ++i) out_buf_.events[out_buf_.count++] = notes_in->events[i];
        }

        alloc_.process_note_buffer(
            notes_in, frame_counter_,
            [this](int slot, int /*note*/, float /*vel*/, uint32_t /*offset*/, uint64_t note_id) {
                // Note-on: reset per-channel state for this slot. Phase is
                // randomized from note_id so simultaneous chord notes
                // breathe with decorrelated phases.
                for (int c = 0; c < kChannelCount; ++c) {
                    ChannelState& st = slot_state_[slot][c];
                    ChannelParamSnapshot p = snapshot(c);
                    float rand_unit = hash_to_unit(hash_note_id(note_id, c));
                    float offset = p.phase_offset + rand_unit * p.phase_random;
                    offset -= std::floor(offset);
                    if (offset < 0.0f) offset += 1.0f;
                    // Split: phase_random_offset is applied on top of any
                    // phase source (free / external / metronome). The free-
                    // mode accumulator starts at 0; v1 free-mode behavior
                    // (st.phase = offset at note-on) is preserved bit-for-
                    // bit because the per-block evaluator computes
                    // phase = phase_accum + phase_random_offset.
                    st.phase_random_offset = offset;
                    st.phase_accum = 0.0;
                    st.phase = offset;
                    st.prev_phase = offset;
                    st.attack_progress = 0.0f;
                    st.rng = hash_note_id(note_id ^ 0xA5A5A5A5u, c);
                    // Seed S&H / smooth_random history from this note's RNG
                    // so sample_hold and smooth_random produce a meaningful
                    // value on the FIRST sample after note-on (instead of 0
                    // until the LFO phase first wraps — which can be never
                    // for slow rates).
                    st.sh_prev2 = lcg_unit_signed(st.rng);
                    st.sh_prev  = lcg_unit_signed(st.rng);
                    st.sh_next  = lcg_unit_signed(st.rng);
                    st.sh_value = lcg_unit_signed(st.rng);
                    slot_inited_[slot][c] = true;
                }
            },
            [](int /*slot*/, int /*note*/, uint64_t /*note_id*/) {
                // Note-off: leave per-channel state intact; the synth's
                // own allocator decides voice end.
            },
            [](int /*slot*/, VividNoteEventType /*kind*/, float /*value*/) {});

        // ── Pass 2: per active slot, per enabled channel, advance the
        // LFO phase by one buffer-block of time, evaluate the waveform,
        // compose with upstream slot value, and emit one expression event
        // onto out_buf_.
        const float dt_per_block =
            static_cast<float>(ctx->buffer_size) / static_cast<float>(ctx->sample_rate);

        // Pull the graph metronome and external beat_phase scalar once per
        // call. Used per-channel based on clock_mode. beat_phase_in is read
        // at block start because the modulator advances at block rate.
        const vivid::MetronomeTransport metronome = vivid::metronome_transport(ctx);
        const float beat_phase_in = vivid::audio_scalar_block_start(ctx, kBeatPhasePortIdx);

        for (int slot = 0; slot < kMaxVoices; ++slot) {
            const auto& s = alloc_.slots[slot];
            if (!s.active) continue;
            if (s.note_id == 0) continue;

            for (int c = 0; c < kChannelCount; ++c) {
                ChannelParamSnapshot p = snapshot(c);
                if (p.amount <= 0.0f) continue;        // channel disabled
                if (!slot_inited_[slot][c]) continue;  // shouldn't happen, but safe

                ChannelState& st = slot_state_[slot][c];

                // Compute phase from whichever source the clock_mode picks.
                // Per-note phase_random_offset is layered on top in all
                // modes so simultaneous notes can still be decorrelated
                // even when the global metronome makes the base phase
                // shared across voices.
                double base_phase;
                switch (p.clock_mode) {
                    case vivid::kClockModeMetronome:
                        base_phase = vivid::cycle_phase_from_total_beats(
                            metronome.beats_elapsed, p.sync_division, /*phase_offset=*/0.0);
                        break;
                    case vivid::kClockModeExternal:
                        base_phase = std::fmod(
                            static_cast<double>(beat_phase_in) * static_cast<double>(p.rate_hz), 1.0);
                        if (base_phase < 0.0) base_phase += 1.0;
                        break;
                    case vivid::kClockModeInternal:
                    default:
                        st.phase_accum += static_cast<double>(p.rate_hz) * static_cast<double>(dt_per_block);
                        st.phase_accum -= std::floor(st.phase_accum);
                        base_phase = st.phase_accum;
                        break;
                }
                double composite = base_phase + static_cast<double>(st.phase_random_offset);
                composite -= std::floor(composite);
                st.phase = static_cast<float>(composite);

                // Advance attack envelope toward 1.0.
                if (p.attack_ms > 0.0f) {
                    float attack_inc = dt_per_block * 1000.0f / p.attack_ms;
                    st.attack_progress = std::min(1.0f, st.attack_progress + attack_inc);
                } else {
                    st.attack_progress = 1.0f;
                }

                // Evaluate raw waveform in [-1, 1].
                float raw = eval_waveform(p.waveform, st, st.phase);

                // Polarity: bipolar (-1..1) or unipolar (0..1).
                if (p.polarity == 1) raw = raw * 0.5f + 0.5f;

                // Scale by amount and attack envelope.
                float mod_value = raw * p.amount * st.attack_progress;

                // Compose with upstream slot value (or replace).
                float upstream = 0.0f;
                switch (c) {
                    case kChannelTimbre:    upstream = s.timbre;            break;
                    case kChannelPressure:  upstream = s.pressure;          break;
                    case kChannelPitchBend: upstream = s.pitch_bend_semis;  break;
                }
                bool replace = (p.mode == 1);
                float composed = replace ? mod_value : (upstream + mod_value);

                // Clamp to channel range.
                switch (c) {
                    case kChannelTimbre:    composed = std::clamp(composed, 0.0f, 1.0f);    break;
                    case kChannelPressure:  composed = std::clamp(composed, 0.0f, 1.0f);    break;
                    case kChannelPitchBend: composed = std::clamp(composed, -48.0f, 48.0f); break;
                }

                // Emit event onto out_buf_. note_helpers' push() handles
                // the buffer-full case gracefully (returns false, no-op).
                switch (c) {
                    case kChannelTimbre:
                        vivid_sequencers::note_timbre(out_buf_, s.note_id, composed);
                        break;
                    case kChannelPressure:
                        vivid_sequencers::note_pressure(out_buf_, s.note_id, composed);
                        break;
                    case kChannelPitchBend:
                        vivid_sequencers::note_pitch_bend(out_buf_, s.note_id, composed);
                        break;
                }
            }
        }

        // Publish the merged buffer.
        if (ctx->custom_outputs && ctx->custom_output_count > 0) {
            ctx->custom_outputs[0] = &out_buf_;
        }

        frame_counter_ += ctx->buffer_size;
    }
};

VIVID_DEFINE_OP(NoteModulator) {
}

