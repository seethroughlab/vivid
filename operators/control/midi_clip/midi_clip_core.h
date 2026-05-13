#pragma once
#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/metronome_sync.h"
#include "operator_api/editor_ui.h"
#include "note_helpers.h"
#include "note_id_counter.h"
#include "midi_clip_editor_shared.h"
#include "common/midi_file.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

struct MidiClipCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = true;

    // --- Params -------------------------------------------------------
    // length_bars: actual bar count (arbitrary float, e.g. 197.0 for a 197-bar piece)
    vivid::Param<float>            length_bars   {"length_bars", 2.0f, 0.25f, 1024.0f};
    // quantize_grid: enum index 0=1/32 1=1/16 2=1/8 3=1/4
    vivid::Param<int>              quantize_grid {"quantize_grid", 1, {"1/32","1/16","1/8","1/4"}};
    vivid::Param<vivid::TextValue> pattern_data  {"pattern_data",  "[]"};
    vivid::Param<vivid::FilePath>  file          {"file"};
    vivid::Param<bool>             playing       {"playing", true};
    vivid::Param<bool>             loop          {"loop", false};
    vivid::Param<int>              transpose     {"transpose", 0, -48, 48};
    vivid::Param<float>            velocity_scale {"velocity_scale", 1.0f, 0.0f, 4.0f};
    // midi_import: legacy hidden alias. New drops write directly to `file`.
    vivid::Param<vivid::FilePath>  midi_import   {"midi_import"};
    vivid::Param<vivid::TextValue> playback_pos_ {"_playback_pos"};
    // loop region: both 0 = full pattern loops; le > ls = loop that sub-region only
    vivid::Param<float>            loop_start_beat {"loop_start_beat", 0.0f, 0.0f, 256.0f};
    vivid::Param<float>            loop_end_beat   {"loop_end_beat",   0.0f, 0.0f, 256.0f};

    MidiClipCore() {
        vivid::semantic_shape(file, "path");
        vivid::description(file, "Path to a .mid MIDI file to play");
        vivid::description(playing, "Enable or disable playback");
        vivid::description(loop, "Loop the MIDI file when it reaches the end");
        vivid::description(transpose, "Shift all notes up or down in semitones (-48 to +48)");
        vivid::description(velocity_scale, "Scale note velocities (1 = original, 0 = silent)");
        pattern_data.display_hint    = VIVID_DISPLAY_HIDDEN;
        midi_import.display_hint     = VIVID_DISPLAY_HIDDEN;
        playback_pos_.display_hint   = VIVID_DISPLAY_HIDDEN;
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

    struct FileActiveNote {
        uint8_t channel = 0;
        uint8_t note = 0;
        uint64_t note_id = 0;
    };
    FileActiveNote file_active_notes_[kMaxActive] = {};
    int        file_active_count_ = 0;
    double     file_transport_seconds_ = 0.0;
    size_t     file_next_event_index_ = 0;
    uint64_t   file_audio_generation_ = 0;

    // --- Cross-thread pattern (guarded by pattern_mutex_) -------------
    std::mutex                           pattern_mutex_;
    std::vector<midi_clip::ParsedNote>   audio_notes_;

    // --- Main-thread parsing scratch ----------------------------------
    std::string                          cached_pattern_str_;
    std::vector<midi_clip::ParsedNote>   pending_notes_;

    struct SequenceData {
        vivid::midi_file::Sequence sequence;
        std::string path;
    };
    std::atomic<SequenceData*> sequence_{nullptr};
    SequenceData* deferred_delete_ = nullptr;
    std::atomic<uint64_t> sequence_generation_{1};
    std::atomic<float> transport_seconds_atomic_{0.0f};
    std::atomic<uint64_t> audio_generation_atomic_{0};
    std::string last_file_path_;
    std::string file_error_;
    size_t      file_note_count_ = 0;
    double      file_duration_seconds_ = 0.0;
    bool        file_loaded_ = false;

    // --- Editor state (main thread only) ------------------------------
    float  editor_scroll_pitch_  = 72.0f; // MIDI pitch at top of visible grid
    float  editor_scroll_x_      = 0.0f;  // horizontal scroll offset in beats
    float  editor_zoom_beats_    = 0.0f;  // 0 = full pattern view; >0 = beats visible
    float  row_h_                = 14.0f; // row height in px; Option+scroll to adjust (6–40)
    float  last_lb_val_          = -1.0f; // detect length_bars changes to reset zoom
    bool   hscroll_dragging_           = false;
    float  hscroll_drag_start_scroll_  = 0.0f;

    // Fold / scale state (editor display only, not persisted as params)
    bool   fold_rows_    = false;
    int    scale_root_   = -1;  // -1=off, 0=C … 11=B
    int    scale_type_   = 0;   // 0=Major 1=NatMinor 2=HarmMinor 3=PentaMaj 4=PentaMin

    enum class DragMode { None, AddingNote, MovingNote, ResizingNote,
                          VelocityDrag, PitchBendDrag, PressureDrag,
                          LoopBraceSweep, LoopBraceLeft, LoopBraceRight, LoopBraceBody,
                          BoxSelect };
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
    // Parallel selection flags — kept same size as editor_notes_ (0=unselected, 1=selected)
    std::vector<uint8_t> note_selected_;
    // Last value we submitted via set_string_param. Used to distinguish
    // our own writes from external changes (undo, graph reload).
    std::string editor_submitted_str_;
    uint64_t    editor_file_generation_ = 0;
    float       editor_file_bpm_ = 0.0f;

    // vertical scroll state (simple, for piano-roll pitch axis)
    float scroll_y_     = 0.0f;
    bool  scrollbar_dragging_ = false;
    float scrollbar_drag_start_scroll_ = 0.0f;

    // Box-select state (main thread only)
    double  box_sel_start_beat_  = 0.0;
    int     box_sel_start_pitch_ = 0;

    // Length text-field state (main thread only)
    vivid::ui::TextFieldState  length_field_state_;
    char                       length_field_buf_[32] = "2";

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
        out.push_back(&file);
        out.push_back(&playing);
        out.push_back(&loop);
        out.push_back(&transpose);
        out.push_back(&velocity_scale);
        out.push_back(&midi_import);
        out.push_back(&playback_pos_);
        out.push_back(&loop_start_beat);
        out.push_back(&loop_end_beat);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // scalar output index 0: playhead phase 0..1
        out.push_back({"phase", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        // custom ref output index 0: note event stream
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer));
    }

    ~MidiClipCore() override {
        delete sequence_.load(std::memory_order_relaxed);
        delete deferred_delete_;
    }

    void prepare_instance_assets() override {
        refresh_file_sequence();
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
        refresh_file_sequence();
        if (audio_generation_atomic_.load(std::memory_order_relaxed) > 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.6f",
                          double(transport_seconds_atomic_.load(std::memory_order_relaxed)));
            playback_pos_.str_value = buf;
        }
    }

    void process_audio(const VividAudioContext* ctx);
    void draw_editor(VividEditorContext* ctx);

private:
    void emit_notes_for_block(const VividAudioContext* ctx,
                              const std::vector<midi_clip::ParsedNote>& notes,
                              double loop_origin, double loop_len);
    void stop_all_active_notes(uint32_t frame_offset = 0);
    void stop_all_file_notes(uint32_t frame_offset = 0);
    void refresh_file_sequence();
    void seek_file_events_to(double t, SequenceData* seq);
    void emit_file_message(uint8_t status, uint8_t data1, uint8_t data2, uint32_t frame_offset);
    void emit_file_events_in_range(const vivid::midi_file::Sequence& sequence,
                                   double start_time,
                                   double end_time,
                                   uint32_t frame_base,
                                   uint32_t buffer_size,
                                   uint32_t sample_rate);
    void process_file_audio(const VividAudioContext* ctx, SequenceData* seq);
    bool pattern_mode_active() const;
    void commit_editor_notes(VividEditorContext* ctx);
public:
    void inject_events(const std::vector<std::vector<unsigned char>>& messages);
private:
    std::mutex inject_mutex_;
    std::vector<std::vector<unsigned char>> inject_buffer_;
    void drain_inject(uint32_t frame_offset);
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

inline void MidiClipCore::stop_all_file_notes(uint32_t frame_offset) {
    for (int i = 0; i < file_active_count_; ++i) {
        vivid_sequencers::note_off(notes_buf_,
                                   file_active_notes_[i].note_id,
                                   frame_offset,
                                   file_active_notes_[i].note);
    }
    file_active_count_ = 0;
}

inline bool MidiClipCore::pattern_mode_active() const {
    return file.str_value.empty();
}

inline void MidiClipCore::refresh_file_sequence() {
    delete deferred_delete_;
    deferred_delete_ = nullptr;

    const std::string& path = file.str_value;
    if (path == last_file_path_) return;
    last_file_path_ = path;

    SequenceData* new_seq = nullptr;
    file_error_.clear();
    file_note_count_ = 0;
    file_duration_seconds_ = 0.0;
    file_loaded_ = false;

    if (!path.empty()) {
        auto parsed = vivid::midi_file::parse_file(path);
        if (!parsed.ok()) {
            file_error_ = parsed.error.empty() ? "failed to parse MIDI file" : parsed.error;
            std::fprintf(stderr, "[midi_clip] Failed to parse %s: %s\n",
                         path.c_str(), file_error_.c_str());
        } else {
            file_note_count_ = parsed.note_spans.size();
            file_duration_seconds_ = parsed.duration_seconds;
            const float bpm = std::max(1.0f, audio_bpm_.load(std::memory_order_relaxed));
            const double spb = 60.0 / static_cast<double>(bpm);
            const double beats = spb > 0.0 ? parsed.duration_seconds / spb : 0.0;
            const double bars = std::max(0.25, std::ceil(beats / 4.0));
            length_bars.value = static_cast<float>(std::min(1024.0, bars));

            new_seq = new SequenceData();
            new_seq->path = path;
            new_seq->sequence = std::move(parsed);
            file_loaded_ = true;
        }
    }

    SequenceData* old = sequence_.exchange(new_seq, std::memory_order_acq_rel);
    deferred_delete_ = old;
    sequence_generation_.fetch_add(1, std::memory_order_acq_rel);
}

inline void MidiClipCore::seek_file_events_to(double t, SequenceData* seq) {
    file_next_event_index_ = 0;
    if (!seq) return;
    while (file_next_event_index_ < seq->sequence.events.size() &&
           seq->sequence.events[file_next_event_index_].time_seconds < t) {
        ++file_next_event_index_;
    }
}

inline void MidiClipCore::emit_file_message(
        uint8_t status, uint8_t data1, uint8_t data2, uint32_t frame_offset) {
    uint8_t kind = status & 0xF0u;
    uint8_t ch = status & 0x0Fu;
    bool is_note_on  = (kind == 0x90u) && (data2 > 0);
    bool is_note_off = (kind == 0x80u) || ((kind == 0x90u) && (data2 == 0));

    if (is_note_on) {
        if (file_active_count_ >= kMaxActive) return;
        uint64_t id = vivid_sequencers::next_note_id();
        file_active_notes_[file_active_count_++] = {ch, data1, id};
        vivid_sequencers::note_on(notes_buf_, data1,
                                  static_cast<float>(data2) / 127.0f,
                                  id, frame_offset);
    } else if (is_note_off) {
        for (int i = 0; i < file_active_count_; ++i) {
            if (file_active_notes_[i].channel == ch &&
                file_active_notes_[i].note == data1) {
                vivid_sequencers::note_off(notes_buf_,
                                           file_active_notes_[i].note_id,
                                           frame_offset,
                                           file_active_notes_[i].note);
                for (int j = i; j < file_active_count_ - 1; ++j)
                    file_active_notes_[j] = file_active_notes_[j + 1];
                --file_active_count_;
                return;
            }
        }
    }
}

inline void MidiClipCore::emit_file_events_in_range(
        const vivid::midi_file::Sequence& sequence,
        double start_time,
        double end_time,
        uint32_t frame_base,
        uint32_t buffer_size,
        uint32_t sample_rate) {
    while (file_next_event_index_ < sequence.events.size() &&
           sequence.events[file_next_event_index_].time_seconds < start_time) {
        ++file_next_event_index_;
    }

    while (file_next_event_index_ < sequence.events.size()) {
        const auto& ev = sequence.events[file_next_event_index_];
        if (ev.time_seconds >= end_time) break;

        double rel_seconds = std::max(0.0, ev.time_seconds - start_time);
        uint32_t frame_offset = frame_base + static_cast<uint32_t>(
            std::floor(rel_seconds * static_cast<double>(std::max(1u, sample_rate))));
        frame_offset = std::min(frame_offset, buffer_size > 0 ? buffer_size - 1 : 0);

        uint8_t status = ev.status;
        uint8_t data1 = ev.data1;
        uint8_t data2 = ev.data2;
        uint8_t kind = status & 0xF0u;
        if (kind == 0x80u || kind == 0x90u || kind == 0xA0u) {
            data1 = static_cast<uint8_t>(
                std::clamp(static_cast<int>(data1) + transpose.int_value(), 0, 127));
        }
        if (kind == 0x90u) {
            data2 = static_cast<uint8_t>(std::clamp<int>(
                static_cast<int>(static_cast<float>(data2) * velocity_scale.value), 0, 127));
        }

        emit_file_message(status, data1, data2, frame_offset);
        ++file_next_event_index_;
    }
}

inline void MidiClipCore::inject_events(const std::vector<std::vector<unsigned char>>& messages) {
    std::lock_guard<std::mutex> lock(inject_mutex_);
    for (const auto& m : messages) inject_buffer_.push_back(m);
}

inline void MidiClipCore::drain_inject(uint32_t frame_offset) {
    std::vector<std::vector<unsigned char>> drained;
    {
        std::lock_guard<std::mutex> lock(inject_mutex_);
        drained.swap(inject_buffer_);
    }
    for (const auto& msg : drained) {
        if (msg.size() < 3) continue;
        uint8_t status = msg[0];
        uint8_t kind = status & 0xF0u;
        if (kind == 0x80u || kind == 0x90u)
            emit_file_message(status, msg[1], msg[2], frame_offset);
    }
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

inline void MidiClipCore::process_file_audio(const VividAudioContext* ctx, SequenceData* seq) {
    stop_all_active_notes(0);

    const uint64_t generation = sequence_generation_.load(std::memory_order_acquire);
    if (generation != file_audio_generation_) {
        const bool is_new_instance = (file_audio_generation_ == 0);
        file_audio_generation_ = generation;
        audio_generation_atomic_.store(file_audio_generation_, std::memory_order_relaxed);
        if (is_new_instance && !playback_pos_.str_value.empty()) {
            char* end = nullptr;
            double pos = std::strtod(playback_pos_.str_value.c_str(), &end);
            if (end != playback_pos_.str_value.c_str() && pos > 0.0) {
                file_transport_seconds_ = pos;
                seek_file_events_to(file_transport_seconds_, seq);
            } else {
                file_transport_seconds_ = 0.0;
                file_next_event_index_ = 0;
            }
        } else {
            file_transport_seconds_ = 0.0;
            file_next_event_index_ = 0;
        }
        stop_all_file_notes(0);
    }

    drain_inject(0);
    if (!seq || seq->sequence.events.empty() || seq->sequence.duration_seconds <= 0.0) {
        return;
    }
    if (!playing.bool_value()) {
        stop_all_file_notes(0);
        return;
    }

    const double sample_rate = std::max(1u, ctx->sample_rate);
    uint32_t frame_cursor = 0;
    while (frame_cursor < ctx->buffer_size) {
        const double remaining_seconds =
            static_cast<double>(ctx->buffer_size - frame_cursor) / sample_rate;
        double segment_end = file_transport_seconds_ + remaining_seconds;
        bool wraps = false;
        uint32_t segment_frames = ctx->buffer_size - frame_cursor;

        if (loop.bool_value() && segment_end >= seq->sequence.duration_seconds) {
            wraps = true;
            double to_boundary = std::max(0.0,
                seq->sequence.duration_seconds - file_transport_seconds_);
            segment_frames = static_cast<uint32_t>(std::min<double>(
                ctx->buffer_size - frame_cursor,
                std::max(0.0, std::round(to_boundary * sample_rate))));
            if (segment_frames == 0 && ctx->buffer_size > frame_cursor)
                segment_frames = 1;
            segment_end = file_transport_seconds_ +
                static_cast<double>(segment_frames) / sample_rate;
        }

        emit_file_events_in_range(seq->sequence, file_transport_seconds_, segment_end,
                                  frame_cursor, ctx->buffer_size, ctx->sample_rate);

        file_transport_seconds_ = segment_end;
        frame_cursor += segment_frames;

        if (wraps) {
            stop_all_file_notes(frame_cursor == 0 ? 0
                : std::min(frame_cursor - 1, ctx->buffer_size - 1));
            file_transport_seconds_ = 0.0;
            file_next_event_index_ = 0;
            continue;
        }

        if (file_transport_seconds_ >= seq->sequence.duration_seconds) {
            stop_all_file_notes(frame_cursor == 0 ? 0
                : std::min(frame_cursor - 1, ctx->buffer_size - 1));
            file_transport_seconds_ = seq->sequence.duration_seconds;
            break;
        }
    }
    transport_seconds_atomic_.store(static_cast<float>(file_transport_seconds_),
                                    std::memory_order_relaxed);
}

inline void MidiClipCore::process_audio(const VividAudioContext* ctx) {
    audio_bpm_.store(static_cast<float>(ctx->metronome_bpm), std::memory_order_relaxed);
    notes_buf_.count = 0;

    SequenceData* seq = sequence_.load(std::memory_order_acquire);
    if (!pattern_mode_active()) {
        process_file_audio(ctx, seq);
        if (ctx->custom_outputs && ctx->custom_output_count > 0)
            ctx->custom_outputs[0] = &notes_buf_;

        const double duration = seq ? seq->sequence.duration_seconds : 0.0;
        const float phase = duration > 0.0
            ? static_cast<float>(std::clamp(file_transport_seconds_ / duration, 0.0, 1.0))
            : 0.0f;
        if (ctx->output_buffers && ctx->output_buffers[0]) {
            for (uint32_t i = 0; i < ctx->buffer_size; ++i)
                ctx->output_buffers[0][i] = phase;
        }
        return;
    }

    stop_all_file_notes(0);
    drain_inject(0);

    // Grab latest parsed pattern from main thread
    std::vector<midi_clip::ParsedNote> local_notes;
    {
        std::lock_guard<std::mutex> lock(pattern_mutex_);
        local_notes = audio_notes_;
    }

    const int    bpb     = static_cast<int>(ctx->metronome_beats_per_bar);
    const double pat_len = static_cast<double>(length_bars.value) * static_cast<double>(bpb);

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
    if (ctx->commands.set_string_param) {
        ctx->commands.set_string_param(ctx->commands.opaque, "pattern_data", s.c_str());
        if (!file.str_value.empty())
            ctx->commands.set_string_param(ctx->commands.opaque, "file", "");
    }
    file.str_value.clear();
    last_file_path_.clear();
}
