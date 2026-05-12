#pragma once
#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/metronome_sync.h"
#include "note_helpers.h"
#include "note_id_counter.h"
#include "midi_clip_editor_shared.h"
#include "common/midi_file.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

struct MidiClipCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = true;

    // --- Params -------------------------------------------------------
    // length_bars: enum index 0=1bar 1=2bars 2=4bars 3=8bars 4=16bars 5=32bars 6=64bars
    vivid::Param<int>              length_bars   {"length_bars",   1,
        {"1 bar","2 bars","4 bars","8 bars","16 bars","32 bars","64 bars"}};
    // quantize_grid: enum index 0=1/32 1=1/16 2=1/8 3=1/4
    vivid::Param<int>              quantize_grid {"quantize_grid", 1, {"1/32","1/16","1/8","1/4"}};
    vivid::Param<vivid::TextValue> pattern_data  {"pattern_data",  "[]"};
    // midi_import: hidden FilePath; set by VIVID_FILE_DROP or MCP set_string_param.
    // draw_editor consumes it, imports notes into pattern_data, then clears it.
    vivid::Param<vivid::FilePath>  midi_import   {"midi_import"};
    // loop region: both 0 = full pattern loops; le > ls = loop that sub-region only
    vivid::Param<float>            loop_start_beat {"loop_start_beat", 0.0f, 0.0f, 256.0f};
    vivid::Param<float>            loop_end_beat   {"loop_end_beat",   0.0f, 0.0f, 256.0f};

    MidiClipCore() {
        pattern_data.display_hint    = VIVID_DISPLAY_HIDDEN;
        midi_import.display_hint     = VIVID_DISPLAY_HIDDEN;
        loop_start_beat.display_hint = VIVID_DISPLAY_HIDDEN;
        loop_end_beat.display_hint   = VIVID_DISPLAY_HIDDEN;
    }

    // --- Audio-thread working state -----------------------------------
    VividNoteBuffer notes_buf_ = {};

    struct ActiveNote {
        uint64_t note_id     = 0;
        uint8_t  pitch       = 0;
        double   end_beat_abs = 0.0;  // absolute metronome beat when note ends
    };
    static constexpr int kMaxActive = VIVID_NOTE_BUFFER_CAPACITY;
    ActiveNote active_notes_[kMaxActive] = {};
    int        active_count_ = 0;
    double     prev_beats_   = -1.0;  // used to detect transport resume after pause

    // --- Cross-thread pattern (guarded by pattern_mutex_) -------------
    std::mutex                           pattern_mutex_;
    std::vector<midi_clip::ParsedNote>   audio_notes_;

    // --- Main-thread parsing scratch ----------------------------------
    std::string                          cached_pattern_str_;
    std::vector<midi_clip::ParsedNote>   pending_notes_;

    // --- Editor state (main thread only) ------------------------------
    float  editor_scroll_pitch_  = 72.0f; // MIDI pitch at top of visible grid
    float  editor_scroll_x_      = 0.0f;  // horizontal scroll offset in beats
    float  editor_zoom_beats_    = 0.0f;  // 0 = full pattern view; >0 = beats visible
    float  row_h_                = 14.0f; // row height in px; Option+scroll to adjust (6–40)
    int    last_lb_idx_          = -1;    // detect length_bars changes to reset zoom
    bool   hscroll_dragging_           = false;
    float  hscroll_drag_start_scroll_  = 0.0f;

    // Fold / scale state (editor display only, not persisted as params)
    bool   fold_rows_    = false;
    int    scale_root_   = -1;  // -1=off, 0=C … 11=B
    int    scale_type_   = 0;   // 0=Major 1=NatMinor 2=HarmMinor 3=PentaMaj 4=PentaMin

    enum class DragMode { None, AddingNote, MovingNote, ResizingNote,
                          VelocityDrag, PitchBendDrag, PressureDrag,
                          LoopBraceSweep, LoopBraceLeft, LoopBraceRight, LoopBraceBody };
    DragMode drag_mode_       = DragMode::None;
    int      drag_note_idx_   = -1;
    float    drag_start_mx_   = 0.0f;  // mouse X (screen) when drag began
    float    drag_start_my_   = 0.0f;  // mouse Y (screen) when drag began
    double   drag_orig_start_ = 0.0;   // note's start_beat at drag start
    double   drag_orig_dur_   = 0.0;   // note's duration_beats at drag start
    uint8_t  drag_orig_pitch_ = 60;    // note's pitch at drag start
    float    drag_orig_vel_   = 0.8f;  // note's velocity at drag start
    float    drag_orig_pb_    = 0.0f;  // note's pitch_bend at drag start
    float    drag_orig_pres_  = 0.0f;  // note's pressure at drag start
    double   drag_orig_loop_start_ = 0.0;
    double   drag_orig_loop_end_   = 0.0;

    // working copy of notes in the editor (refreshed from string_param_values)
    std::vector<midi_clip::ParsedNote> editor_notes_;
    // Last value we submitted via set_string_param. Used to distinguish
    // our own writes from external changes (undo, graph reload).
    std::string editor_submitted_str_;

    // vertical scroll state (simple, for piano-roll pitch axis)
    float scroll_y_     = 0.0f;
    bool  scrollbar_dragging_ = false;
    float scrollbar_drag_start_scroll_ = 0.0f;

    // --- MIDI import state (main thread only) -------------------------
    std::string         last_import_path_;     // last path we imported from
    std::string         pending_import_path_;  // path ready for draw_editor to consume
    bool                has_pending_import_ = false;
    std::atomic<float>  audio_bpm_{120.0f};    // updated from process_audio
    // Status display in editor header
    std::string         import_status_;
    double              import_status_until_ = 0.0;

    // --- Operator API -------------------------------------------------

    static VividEditorMetadata editor_metadata() {
        VividEditorMetadata m{};
        m.default_width  = 1100;
        m.default_height = 640;
        m.min_width      = 700;
        m.min_height     = 400;
        m.title_suffix   = "MIDI Clip";
        return m;
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&length_bars);
        out.push_back(&quantize_grid);
        out.push_back(&pattern_data);
        out.push_back(&midi_import);
        out.push_back(&loop_start_beat);
        out.push_back(&loop_end_beat);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // scalar output index 0: playhead phase 0..1
        out.push_back({"phase", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        // custom ref output index 0: note event stream
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer));
    }

    void main_thread_update(double /*time*/) override {
        // _vivid_sync_params has already updated pattern_data.str_value.
        const std::string& s = pattern_data.str_value;
        if (s != cached_pattern_str_) {
            cached_pattern_str_ = s;
            midi_clip::parse_pattern(s, pending_notes_);
            std::lock_guard<std::mutex> lock(pattern_mutex_);
            audio_notes_ = pending_notes_;
        }
        // Detect new MIDI import path (set by VIVID_FILE_DROP or MCP set_string_param).
        const std::string& imp = midi_import.str_value;
        if (!imp.empty() && imp != last_import_path_) {
            last_import_path_    = imp;
            pending_import_path_ = imp;
            has_pending_import_  = true;
        }
    }

    void process_audio(const VividAudioContext* ctx);
    void draw_editor(VividEditorContext* ctx);

private:
    void emit_notes_for_block(const VividAudioContext* ctx,
                              const std::vector<midi_clip::ParsedNote>& notes,
                              double loop_origin, double loop_len);
    void stop_all_active_notes(uint32_t frame_offset = 0);
    void commit_editor_notes(VividEditorContext* ctx);
};

// ---------------------------------------------------------------------------
// process_audio — called on the audio thread for each 256-sample block.
// ---------------------------------------------------------------------------
inline void MidiClipCore::stop_all_active_notes(uint32_t frame_offset) {
    for (int i = 0; i < active_count_; ++i) {
        vivid_sequencers::note_off(notes_buf_, active_notes_[i].note_id,
                                   frame_offset, active_notes_[i].pitch);
    }
    active_count_ = 0;
}

inline void MidiClipCore::emit_notes_for_block(
        const VividAudioContext* ctx,
        const std::vector<midi_clip::ParsedNote>& notes,
        double loop_origin, double loop_len) {
    const double bpm         = static_cast<double>(ctx->metronome_bpm);
    const double sr          = static_cast<double>(ctx->sample_rate);
    const double beats_start = ctx->metronome_beats_elapsed;
    const double bps         = (bpm > 0.0 && sr > 0.0) ? (bpm / 60.0 / sr) : 0.0;
    if (bps <= 0.0) return;

    const double block_beats = ctx->buffer_size * bps;
    const double beats_end   = beats_start + block_beats;

    // --- Expire / emit active note-offs first (always, even if pattern is empty) ---
    int dst = 0;
    for (int i = 0; i < active_count_; ++i) {
        const double end_abs = active_notes_[i].end_beat_abs;
        if (end_abs < beats_start) {
            vivid_sequencers::note_off(notes_buf_, active_notes_[i].note_id,
                                       0, active_notes_[i].pitch);
        } else if (end_abs < beats_end) {
            uint32_t off = static_cast<uint32_t>((end_abs - beats_start) / bps);
            off = std::min(off, ctx->buffer_size - 1u);
            vivid_sequencers::note_off(notes_buf_, active_notes_[i].note_id,
                                       off, active_notes_[i].pitch);
        } else {
            active_notes_[dst++] = active_notes_[i]; // still sounding
        }
    }
    active_count_ = dst;

    if (loop_len <= 0.0 || notes.empty()) return;

    // Position within loop at block start
    const double pos_in_loop       = std::fmod(beats_start, loop_len);
    const double pat_start         = loop_origin + pos_in_loop;
    const double pat_end_unwrapped = pat_start + block_beats;
    const bool   wraps             = pat_end_unwrapped >= loop_origin + loop_len;

    // --- Fire note-ons for notes whose start_beat falls in this block ---
    for (const auto& n : notes) {
        if (active_count_ >= kMaxActive) break;

        double offset_beats = -1.0;
        const double sb = n.start_beat;

        if (!wraps) {
            if (sb >= pat_start && sb < pat_end_unwrapped)
                offset_beats = sb - pat_start;
        } else {
            // Block crosses loop boundary: [pat_start, lo+loop_len) ∪ [lo, pat_end_unwrapped-loop_len)
            if (sb >= pat_start && sb < loop_origin + loop_len)
                offset_beats = sb - pat_start;
            else if (sb >= loop_origin && sb < pat_end_unwrapped - loop_len)
                offset_beats = (loop_origin + loop_len - pat_start) + (sb - loop_origin);
        }

        if (offset_beats < 0.0) continue;

        uint32_t off = static_cast<uint32_t>(offset_beats / bps);
        off = std::min(off, ctx->buffer_size - 1u);

        uint64_t id = vivid_sequencers::next_note_id();
        if (!vivid_sequencers::note_on(notes_buf_,
                static_cast<uint8_t>(n.pitch), n.velocity, id, off)) {
            break; // buffer full
        }
        if (n.pitch_bend != 0.0f)
            vivid_sequencers::note_pitch_bend(notes_buf_, id, n.pitch_bend, off);
        if (n.pressure > 0.0f)
            vivid_sequencers::note_pressure(notes_buf_, id, n.pressure, off);

        if (active_count_ < kMaxActive) {
            active_notes_[active_count_++] = {
                id, n.pitch,
                beats_start + offset_beats + n.duration_beats
            };
        }
    }
}

inline void MidiClipCore::process_audio(const VividAudioContext* ctx) {
    audio_bpm_.store(static_cast<float>(ctx->metronome_bpm), std::memory_order_relaxed);
    notes_buf_.count = 0;

    // Grab latest parsed pattern from main thread
    std::vector<midi_clip::ParsedNote> local_notes;
    {
        std::lock_guard<std::mutex> lock(pattern_mutex_);
        local_notes = audio_notes_;
    }

    const int    lb_idx  = length_bars.int_value();
    const int    bpb     = static_cast<int>(ctx->metronome_beats_per_bar);
    const double pat_len = midi_clip::pattern_length_beats(lb_idx, bpb);

    // Resolve loop region: both 0 (or le <= ls) → full pattern
    const double raw_ls = static_cast<double>(loop_start_beat.value);
    const double raw_le = static_cast<double>(loop_end_beat.value);
    const double ls = std::clamp(raw_ls, 0.0, pat_len);
    const double le = std::clamp(raw_le, 0.0, pat_len);
    const bool   has_loop    = (le > ls + 1e-6);
    const double loop_origin = has_loop ? ls : 0.0;
    const double loop_len    = has_loop ? (le - ls) : pat_len;

    // Detect transport resume after a pause
    const double beats_now = ctx->metronome_beats_elapsed;
    if (prev_beats_ >= 0.0 && beats_now <= prev_beats_) {
        stop_all_active_notes(0);
    }
    prev_beats_ = beats_now;

    emit_notes_for_block(ctx, local_notes, loop_origin, loop_len);

    // Publish note buffer
    if (ctx->custom_outputs && ctx->custom_output_count > 0)
        ctx->custom_outputs[0] = &notes_buf_;

    // Phase output: 0..1 through the loop window
    const float phase = (loop_len > 0.0)
        ? static_cast<float>(std::fmod(beats_now, loop_len) / loop_len)
        : 0.0f;
    if (ctx->output_buffers && ctx->output_buffers[0]) {
        for (uint32_t i = 0; i < ctx->buffer_size; ++i)
            ctx->output_buffers[0][i] = phase;
    }
}

inline void MidiClipCore::commit_editor_notes(VividEditorContext* ctx) {
    std::string s = midi_clip::serialize_pattern(editor_notes_);
    editor_submitted_str_ = s;
    if (ctx->commands.set_string_param)
        ctx->commands.set_string_param(ctx->commands.opaque, "pattern_data", s.c_str());
}
