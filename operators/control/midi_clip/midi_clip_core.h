#pragma once
#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/metronome_sync.h"
#include "operator_api/editor_ui.h"
#include "operator_api/thumbnail.h"
#include "note_helpers.h"
#include "note_id_counter.h"
#include "midi_clip_editor_shared.h"
#include "common/midi_file.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

struct MidiClipCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = true;

    // --- Params -------------------------------------------------------
    // length_bars: actual bar count (arbitrary float, e.g. 197.0 for a 197-bar piece)
    vivid::Param<float>            length_bars   {"length_bars", 2.0f, 0.25f, 8192.0f};
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
    vivid::Param<vivid::TextValue> clip_data_ref_ {"clip_data_ref"};
    // loop region: both 0 = full pattern loops; le > ls = loop that sub-region only
    vivid::Param<float>            loop_start_beat {"loop_start_beat", 0.0f, 0.0f, 32768.0f};
    vivid::Param<float>            loop_end_beat   {"loop_end_beat",   0.0f, 0.0f, 32768.0f};
    vivid::Param<bool>             editor_fold_     {"_editor_fold", false};
    vivid::Param<int>              editor_scale_root_ {"_editor_scale_root", -1, -1, 11};
    vivid::Param<int>              editor_scale_type_ {"_editor_scale_type", 0, 0, 4};
    vivid::Param<float>            editor_zoom_beat_ {"_editor_zoom_beats", 0.0f, 0.0f, 32768.0f};
    vivid::Param<float>            editor_scroll_beat_ {"_editor_scroll_beat", 0.0f, 0.0f, 32768.0f};
    vivid::Param<float>            editor_row_height_ {"_editor_row_height", 14.0f, 6.0f, 40.0f};

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
        clip_data_ref_.display_hint  = VIVID_DISPLAY_HIDDEN;
        loop_start_beat.display_hint = VIVID_DISPLAY_HIDDEN;
        loop_end_beat.display_hint   = VIVID_DISPLAY_HIDDEN;
        editor_fold_.display_hint = VIVID_DISPLAY_HIDDEN;
        editor_scale_root_.display_hint = VIVID_DISPLAY_HIDDEN;
        editor_scale_type_.display_hint = VIVID_DISPLAY_HIDDEN;
        editor_zoom_beat_.display_hint = VIVID_DISPLAY_HIDDEN;
        editor_scroll_beat_.display_hint = VIVID_DISPLAY_HIDDEN;
        editor_row_height_.display_hint = VIVID_DISPLAY_HIDDEN;
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
    std::vector<midi_clip::ParsedNote>   imported_audio_notes_;

    // --- Main-thread parsing scratch ----------------------------------
    std::string                          cached_pattern_str_;
    std::string                          cached_clip_data_ref_;
    std::string                          clip_data_error_;
    std::string                          clip_data_source_file_;
    double                               clip_data_beat_length_ = 0.0;
    size_t                               clip_data_note_count_ = 0;
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
    double      clip_length_beats_ = 8.0;
    double      clip_length_bars_ = 2.0;
    std::atomic<uint64_t> clip_generation_{1};
    std::atomic<uint32_t> pattern_playhead_raw_{0}; // float bits of clip_playhead_beats_
    double      clip_playhead_beats_ = 0.0;
    bool        clip_finished_ = false;
    bool        audio_was_playing_ = false;
    uint64_t    clip_audio_generation_ = 0;

    // --- Editor state (main thread only) ------------------------------
    float  editor_scroll_pitch_  = 72.0f; // MIDI pitch at top of visible grid
    float  editor_scroll_x_      = 0.0f;  // horizontal scroll offset in beats
    float  editor_zoom_beats_    = 0.0f;  // 0 = full pattern view; >0 = beats visible
    float  row_h_                = 14.0f; // row height in px; Option+scroll to adjust (6–40)
    float  last_lb_val_          = -1.0f; // detect length_bars changes to reset zoom
    bool   editor_view_params_initialized_ = false;
    bool   toolbar_actions_open_ = false;
    bool   toolbar_clear_confirm_ = false;
    vivid::ui::ScrollbarState hscroll_state_{};

    // Fold / scale state (editor display only, not persisted as params)
    bool   fold_rows_    = false;
    int    scale_root_   = -1;  // -1=off, 0=C … 11=B
    int    scale_type_   = 0;   // 0=Major 1=NatMinor 2=HarmMinor 3=PentaMaj 4=PentaMin

    enum class DragMode { None, AddingNote, MovingNote, ResizingNote,
                          PendingEmptyClick,
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
    std::vector<int> editor_note_order_by_start_;
    bool             editor_note_order_dirty_ = true;
    double           editor_max_note_duration_ = 0.0;
    size_t           editor_last_visible_scan_count_ = 0;
    // Parallel selection flags — kept same size as editor_notes_ (0=unselected, 1=selected)
    std::vector<uint8_t> note_selected_;
    // Last value we submitted via set_string_param. Used to distinguish
    // our own writes from external changes (undo, graph reload).
    std::string editor_submitted_str_;
    uint64_t    editor_file_generation_ = 0;
    float       editor_file_bpm_ = 0.0f;
    std::string editor_loaded_clip_data_ref_;

    // vertical scroll state (simple, for piano-roll pitch axis)
    float scroll_y_     = 0.0f;
    bool  scrollbar_dragging_ = false;
    float scrollbar_drag_start_scroll_ = 0.0f;

    // Box-select state (main thread only)
    double  box_sel_start_beat_  = 0.0;
    int     box_sel_start_pitch_ = 0;
    bool    box_sel_additive_ = false;
    std::vector<midi_clip::ParsedNote> drag_orig_notes_;
    std::vector<int> drag_selected_indices_;
    std::vector<midi_clip::ParsedNote> editor_clipboard_;

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

    void draw_thumbnail(const VividThumbnailContext* ctx) override;

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
        out.push_back(&clip_data_ref_);
        out.push_back(&loop_start_beat);
        out.push_back(&loop_end_beat);
        out.push_back(&editor_fold_);
        out.push_back(&editor_scale_root_);
        out.push_back(&editor_scale_type_);
        out.push_back(&editor_zoom_beat_);
        out.push_back(&editor_scroll_beat_);
        out.push_back(&editor_row_height_);
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
        const std::string& clip_ref = clip_data_ref_.str_value;
        if (!clip_ref.empty() && clip_ref != cached_clip_data_ref_) {
            cached_clip_data_ref_ = clip_ref;
            load_clip_data_ref(clip_ref);
        } else if (clip_ref.empty() && s != cached_pattern_str_) {
            cached_pattern_str_ = s;
            midi_clip::parse_pattern(s, pending_notes_);
            std::lock_guard<std::mutex> lock(pattern_mutex_);
            audio_notes_ = pending_notes_;
            clip_generation_.fetch_add(1, std::memory_order_acq_rel);
        }
        // Detect new MIDI import path (set by VIVID_FILE_DROP or MCP set_string_param).
        const std::string& imp = midi_import.str_value;
        if (!imp.empty() && imp != last_import_path_) {
            last_import_path_    = imp;
            pending_import_path_ = imp;
            has_pending_import_  = true;
        }
        refresh_file_sequence();
        if (pattern_mode_active()) {
            if (audio_generation_atomic_.load(std::memory_order_relaxed) > 0) {
                uint32_t raw = pattern_playhead_raw_.load(std::memory_order_relaxed);
                float ph; std::memcpy(&ph, &raw, sizeof(float));
                if (ph >= 0.0f) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "p:%.4f", ph);
                    playback_pos_.str_value = buf;
                }
            }
        } else if (audio_generation_atomic_.load(std::memory_order_relaxed) > 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.6f",
                          double(transport_seconds_atomic_.load(std::memory_order_relaxed)));
            playback_pos_.str_value = buf;
        }
    }

    void process_audio(const VividAudioContext* ctx);
    void draw_editor(VividEditorContext* ctx);
    void draw_inspector(VividInspectorContext* ctx) override {
        if (!ctx) return;
        auto& d = ctx->draw;
        void* o = d.opaque;
        const float x = ctx->content_x;
        const float y = ctx->content_y + 4.0f;
        const float w = ctx->content_width;
        const float h = 58.0f;
        if (d.draw_rect)
            d.draw_rect(o, x, y, w, h, {0.08f, 0.09f, 0.11f, 0.65f});

        std::string source = "authored clip";
        if (!file.str_value.empty())
            source = std::filesystem::path(file.str_value).filename().string();
        else if (!clip_data_source_file_.empty())
            source = std::filesystem::path(clip_data_source_file_).filename().string();
        else if (!clip_data_ref_.str_value.empty())
            source = std::filesystem::path(clip_data_ref_.str_value).filename().string();

        const size_t notes = !clip_data_ref_.str_value.empty()
            ? clip_data_note_count_
            : (!file.str_value.empty() ? file_note_count_ : audio_notes_.size());
        const double beats = clip_length_beats_ > 0.0
            ? clip_length_beats_
            : static_cast<double>(length_bars.value) * 4.0;
        if ((file_loaded_ || !clip_data_ref_.str_value.empty()) &&
            clip_length_bars_ > 0.0 &&
            ctx->commands.set_param && ctx->param_count > 0 && ctx->param_values &&
            std::fabs(ctx->param_values[0] - static_cast<float>(clip_length_bars_)) > 0.001f) {
            ctx->commands.set_param(ctx->commands.opaque, "length_bars",
                                    static_cast<float>(clip_length_bars_));
        }
        const std::string status = !clip_data_error_.empty()
            ? clip_data_error_
            : (!file_error_.empty() ? file_error_ : "ready");

        char meta[256];
        std::snprintf(meta, sizeof(meta), "%zu notes  %.1f beats  %s",
                      notes, beats, status.c_str());
        if (d.draw_text) {
            d.draw_text(o, x + 8.0f, y + 8.0f, source.c_str(),
                        ctx->theme.bright_text, 0.9f);
            d.draw_text(o, x + 8.0f, y + 30.0f, meta,
                        ctx->theme.dim_text, 0.8f);
        }
        ctx->consumed_height = h + 10.0f;
    }

private:
    void emit_notes_for_block(const VividAudioContext* ctx,
                              const std::vector<midi_clip::ParsedNote>& notes,
                              double beats_start,
                              double beats_end,
                              uint32_t frame_base,
                              uint32_t frame_count,
                              int transpose_semitones,
                              float velocity_mul);
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
    bool load_clip_data_ref(const std::string& path);
    void set_clip_length_from_beats(double beats);
    double effective_clip_length_beats(int beats_per_bar) const;
    bool write_clip_data_ref(const std::string& path,
                             const std::vector<midi_clip::ParsedNote>& notes,
                             std::string* error);
    std::string default_clip_data_ref_path() const;
    double editor_notes_beat_length() const;
    void mark_editor_note_order_dirty();
    void rebuild_editor_note_order_if_needed();
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
    return file.str_value.empty() || !clip_data_ref_.str_value.empty();
}

inline void MidiClipCore::set_clip_length_from_beats(double beats) {
    if (!std::isfinite(beats) || beats <= 0.0)
        beats = std::max(1.0f, length_bars.value) * 4.0;
    clip_length_beats_ = std::max(0.25, beats);
    clip_length_bars_ = std::max(0.25, std::ceil(clip_length_beats_ / 4.0));
    length_bars.value = static_cast<float>(std::min(8192.0, clip_length_bars_));
}

inline double MidiClipCore::effective_clip_length_beats(int beats_per_bar) const {
    const int bpb = std::max(1, beats_per_bar);
    if ((!file.str_value.empty() || !clip_data_ref_.str_value.empty()) &&
        clip_length_beats_ > 0.0) {
        return clip_length_beats_;
    }
    return std::max(0.25, static_cast<double>(length_bars.value) *
                              static_cast<double>(bpb));
}

inline double MidiClipCore::editor_notes_beat_length() const {
    double max_beat = 0.0;
    for (const auto& n : editor_notes_) {
        max_beat = std::max(max_beat, n.start_beat + n.duration_beats);
    }
    return max_beat;
}

inline void MidiClipCore::mark_editor_note_order_dirty() {
    editor_note_order_dirty_ = true;
}

inline void MidiClipCore::rebuild_editor_note_order_if_needed() {
    if (!editor_note_order_dirty_ &&
        editor_note_order_by_start_.size() == editor_notes_.size()) {
        return;
    }
    editor_note_order_by_start_.resize(editor_notes_.size());
    editor_max_note_duration_ = 0.0;
    for (int i = 0; i < static_cast<int>(editor_notes_.size()); ++i) {
        editor_note_order_by_start_[static_cast<size_t>(i)] = i;
        editor_max_note_duration_ = std::max(editor_max_note_duration_,
                                             editor_notes_[static_cast<size_t>(i)].duration_beats);
    }
    std::sort(editor_note_order_by_start_.begin(), editor_note_order_by_start_.end(),
        [&](int a, int b) {
            const auto& na = editor_notes_[static_cast<size_t>(a)];
            const auto& nb = editor_notes_[static_cast<size_t>(b)];
            if (na.start_beat == nb.start_beat) return a < b;
            return na.start_beat < nb.start_beat;
        });
    editor_note_order_dirty_ = false;
}

inline std::string MidiClipCore::default_clip_data_ref_path() const {
    const std::string seed = !file.str_value.empty()
        ? file.str_value
        : (!clip_data_ref_.str_value.empty() ? clip_data_ref_.str_value : "midi_clip");
    std::size_t h = std::hash<std::string>{}(seed);
    std::ostringstream name;
    name << "clip_" << std::hex << h << ".mclip.json";
    std::filesystem::path dir = std::filesystem::current_path() / ".vivid" / "clips";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / name.str()).string();
}

inline bool MidiClipCore::write_clip_data_ref(
        const std::string& path,
        const std::vector<midi_clip::ParsedNote>& notes,
        std::string* error) {
    try {
        nlohmann::json root;
        root["version"] = 1;
        root["source_file"] = !clip_data_source_file_.empty()
            ? clip_data_source_file_
            : file.str_value;
        root["beat_length"] = editor_notes_beat_length();
        root["note_count"] = notes.size();
        root["notes"] = nlohmann::json::parse(midi_clip::serialize_pattern(notes));

        std::filesystem::path out_path(path);
        std::error_code ec;
        if (out_path.has_parent_path())
            std::filesystem::create_directories(out_path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "failed to open clip sidecar for writing";
            return false;
        }
        out << root.dump(2);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

inline bool MidiClipCore::load_clip_data_ref(const std::string& path) {
    clip_data_error_.clear();
    clip_data_source_file_.clear();
    clip_data_beat_length_ = 0.0;
    clip_data_note_count_ = 0;
    pending_notes_.clear();
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            clip_data_error_ = "failed to open clip sidecar";
            std::lock_guard<std::mutex> lock(pattern_mutex_);
            audio_notes_.clear();
            return false;
        }
        nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
        if (root.is_discarded() || !root.is_object() || !root.contains("notes")) {
            clip_data_error_ = "invalid clip sidecar";
            std::lock_guard<std::mutex> lock(pattern_mutex_);
            audio_notes_.clear();
            return false;
        }
        midi_clip::parse_pattern(root["notes"].dump(), pending_notes_);
        clip_data_source_file_ = root.value("source_file", "");
        clip_data_beat_length_ = root.value("beat_length", 0.0);
        clip_data_note_count_ = pending_notes_.size();
        {
            std::lock_guard<std::mutex> lock(pattern_mutex_);
            audio_notes_ = pending_notes_;
            imported_audio_notes_.clear();
        }
        if (clip_data_beat_length_ > 0.0) {
            set_clip_length_from_beats(clip_data_beat_length_);
        }
        clip_generation_.fetch_add(1, std::memory_order_acq_rel);
        return true;
    } catch (const std::exception& e) {
        clip_data_error_ = e.what();
        std::lock_guard<std::mutex> lock(pattern_mutex_);
        audio_notes_.clear();
        clip_generation_.fetch_add(1, std::memory_order_acq_rel);
        return false;
    }
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
    if (clip_data_ref_.str_value.empty())
        set_clip_length_from_beats(static_cast<double>(length_bars.value) * 4.0);

    if (!path.empty()) {
        auto parsed = vivid::midi_file::parse_file(path);
        if (!parsed.ok()) {
            file_error_ = parsed.error.empty() ? "failed to parse MIDI file" : parsed.error;
            std::fprintf(stderr, "[midi_clip] Failed to parse %s: %s\n",
                         path.c_str(), file_error_.c_str());
            std::lock_guard<std::mutex> lock(pattern_mutex_);
            imported_audio_notes_.clear();
        } else {
            std::vector<midi_clip::ParsedNote> imported_notes;
            imported_notes.reserve(parsed.note_spans.size());
            double max_beat = 0.0;
            for (const auto& span : parsed.note_spans) {
                midi_clip::ParsedNote n{};
                n.pitch = span.pitch;
                n.start_beat = span.start_beats;
                n.duration_beats = std::max(0.01, span.duration_beats);
                n.velocity = std::clamp(static_cast<float>(span.velocity) / 127.0f,
                                        0.01f, 1.0f);
                imported_notes.push_back(n);
                max_beat = std::max(max_beat, n.start_beat + n.duration_beats);
            }

            file_note_count_ = parsed.note_spans.size();
            file_duration_seconds_ = parsed.duration_seconds;
            const double beats = std::max(parsed.duration_beats, max_beat);
            set_clip_length_from_beats(beats);
            {
                std::lock_guard<std::mutex> lock(pattern_mutex_);
                imported_audio_notes_ = std::move(imported_notes);
            }

            new_seq = new SequenceData();
            new_seq->path = path;
            new_seq->sequence = std::move(parsed);
            file_loaded_ = true;
        }
    } else {
        std::lock_guard<std::mutex> lock(pattern_mutex_);
        imported_audio_notes_.clear();
    }

    SequenceData* old = sequence_.exchange(new_seq, std::memory_order_acq_rel);
    deferred_delete_ = old;
    sequence_generation_.fetch_add(1, std::memory_order_acq_rel);
    clip_generation_.fetch_add(1, std::memory_order_acq_rel);
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
        double beats_start,
        double beats_end,
        uint32_t frame_base,
        uint32_t frame_count,
        int transpose_semitones,
        float velocity_mul) {
    (void)frame_count;
    const double bpm         = static_cast<double>(ctx->metronome_bpm);
    const double sr          = static_cast<double>(ctx->sample_rate);
    const double bps         = (bpm > 0.0 && sr > 0.0) ? (bpm / 60.0 / sr) : 0.0;
    if (bps <= 0.0) return;

    const double block_beats = std::max(0.0, beats_end - beats_start);
    if (block_beats <= 0.0 || frame_count == 0) return;
    const uint32_t buffer_last = ctx->buffer_size > 0 ? ctx->buffer_size - 1u : 0u;

    // --- Expire / emit active note-offs first (always, even if pattern is empty) ---
    int dst = 0;
    for (int i = 0; i < active_count_; ++i) {
        const double end_abs = active_notes_[i].end_beat_abs;
        if (end_abs < beats_start) {
            vivid_sequencers::note_off(notes_buf_, active_notes_[i].note_id,
                                       std::min(frame_base, buffer_last),
                                       active_notes_[i].pitch);
        } else if (end_abs < beats_end) {
            uint32_t off = frame_base +
                static_cast<uint32_t>((end_abs - beats_start) / bps);
            off = std::min(off, buffer_last);
            vivid_sequencers::note_off(notes_buf_, active_notes_[i].note_id,
                                       off, active_notes_[i].pitch);
        } else {
            active_notes_[dst++] = active_notes_[i]; // still sounding
        }
    }
    active_count_ = dst;

    if (notes.empty()) return;

    // --- Fire note-ons for notes whose start_beat falls in this block ---
    for (const auto& n : notes) {
        if (active_count_ >= kMaxActive) break;

        auto emit_note_at = [&](double offset_beats) -> bool {
            if (offset_beats < 0.0 || offset_beats >= block_beats ||
                active_count_ >= kMaxActive) {
                return true;
            }

            uint32_t off = frame_base + static_cast<uint32_t>(offset_beats / bps);
            off = std::min(off, buffer_last);

            uint64_t id = vivid_sequencers::next_note_id();
            const uint8_t out_pitch = static_cast<uint8_t>(
                std::clamp(static_cast<int>(n.pitch) + transpose_semitones, 0, 127));
            const float out_velocity = std::clamp(n.velocity * velocity_mul, 0.0f, 1.0f);
            if (!vivid_sequencers::note_on(notes_buf_,
                    out_pitch, out_velocity, id, off)) {
                return false;
            }
            if (n.pitch_bend != 0.0f)
                vivid_sequencers::note_pitch_bend(notes_buf_, id, n.pitch_bend, off);
            if (n.pressure > 0.0f)
                vivid_sequencers::note_pressure(notes_buf_, id, n.pressure, off);

            const double end_abs = beats_start + offset_beats + n.duration_beats;
            if (end_abs < beats_end) {
                uint32_t note_off_frame = frame_base +
                    static_cast<uint32_t>((end_abs - beats_start) / bps);
                note_off_frame = std::min(note_off_frame, buffer_last);
                vivid_sequencers::note_off(notes_buf_, id, note_off_frame, out_pitch);
            } else if (active_count_ < kMaxActive) {
                active_notes_[active_count_++] = {
                    id, out_pitch,
                    end_abs
                };
            }
            return true;
        };

        if (n.start_beat >= beats_start && n.start_beat < beats_end) {
            if (!emit_note_at(n.start_beat - beats_start)) break;
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

    stop_all_file_notes(0);
    drain_inject(0);
    const bool playing_now = playing.bool_value();
    const bool is_first_pattern_call = (clip_audio_generation_ == 0);
    const uint64_t clip_generation = clip_generation_.load(std::memory_order_acquire);
    if (clip_generation != clip_audio_generation_) {
        clip_audio_generation_ = clip_generation;
        if (is_first_pattern_call) {
            // New instance after recompile: restore persisted playhead position.
            const std::string& pos_str = playback_pos_.str_value;
            if (pos_str.size() > 2 && pos_str[0] == 'p' && pos_str[1] == ':') {
                char* end = nullptr;
                double beats = std::strtod(pos_str.c_str() + 2, &end);
                if (end != pos_str.c_str() + 2 && beats >= 0.0)
                    clip_playhead_beats_ = beats;
            }
            // Signal that audio thread is running, unblocking main_thread_update writes.
            audio_generation_atomic_.store(1, std::memory_order_relaxed);
        } else {
            // Genuine content change: stop active notes but keep playhead position.
            clip_finished_ = false;
            stop_all_active_notes(0);
        }
    }

    if (!playing_now) {
        stop_all_active_notes(0);
        audio_was_playing_ = false;
        if (ctx->custom_outputs && ctx->custom_output_count > 0)
            ctx->custom_outputs[0] = &notes_buf_;
        if (ctx->output_buffers && ctx->output_buffers[0]) {
            for (uint32_t i = 0; i < ctx->buffer_size; ++i)
                ctx->output_buffers[0][i] = 0.0f;
        }
        return;
    }

    // Grab latest parsed pattern from main thread
    std::vector<midi_clip::ParsedNote> local_notes;
    {
        std::lock_guard<std::mutex> lock(pattern_mutex_);
        local_notes = pattern_mode_active() ? audio_notes_ : imported_audio_notes_;
    }

    const int    bpb     = static_cast<int>(ctx->metronome_beats_per_bar);
    const double pat_len = effective_clip_length_beats(bpb);

    // Resolve loop region: both 0 (or le <= ls) → full pattern
    const double raw_ls = static_cast<double>(loop_start_beat.value);
    const double raw_le = static_cast<double>(loop_end_beat.value);
    const double ls = std::clamp(raw_ls, 0.0, pat_len);
    const double le = std::clamp(raw_le, 0.0, pat_len);
    const bool   has_loop    = (le > ls + 1e-6);
    const double loop_origin = has_loop ? ls : 0.0;
    const double loop_len    = has_loop ? (le - ls) : pat_len;

    if (!audio_was_playing_ && !is_first_pattern_call) {
        // Genuine resume after pause: restart from beginning.
        clip_playhead_beats_ = (loop.bool_value() && has_loop) ? loop_origin : 0.0;
        clip_finished_ = false;
        stop_all_active_notes(0);
    }
    audio_was_playing_ = true;

    const double bpm = static_cast<double>(ctx->metronome_bpm);
    const double sr = static_cast<double>(ctx->sample_rate);
    const double bps = (bpm > 0.0 && sr > 0.0) ? (bpm / 60.0 / sr) : 0.0;
    const bool loop_enabled = loop.bool_value() && loop_len > 0.0;
    const double boundary_start = loop_enabled ? loop_origin : 0.0;
    const double boundary_end = loop_enabled ? (loop_origin + loop_len) : pat_len;

    if (loop_enabled &&
        (clip_playhead_beats_ < boundary_start || clip_playhead_beats_ >= boundary_end)) {
        clip_playhead_beats_ = boundary_start;
        stop_all_active_notes(0);
    }

    if (bps > 0.0 && !clip_finished_ && pat_len > 0.0) {
        uint32_t frame_cursor = 0;
        while (frame_cursor < ctx->buffer_size) {
            if (!loop_enabled && clip_playhead_beats_ >= pat_len - 1e-9) {
                stop_all_active_notes(frame_cursor == 0 ? 0 :
                    std::min(frame_cursor - 1, ctx->buffer_size - 1));
                clip_finished_ = true;
                break;
            }
            if (loop_enabled && clip_playhead_beats_ >= boundary_end - 1e-9) {
                stop_all_active_notes(frame_cursor == 0 ? 0 :
                    std::min(frame_cursor - 1, ctx->buffer_size - 1));
                clip_playhead_beats_ = boundary_start;
            }

            const uint32_t remaining_frames = ctx->buffer_size - frame_cursor;
            const double beats_to_boundary =
                std::max(0.0, (loop_enabled ? boundary_end : pat_len) - clip_playhead_beats_);
            uint32_t segment_frames = remaining_frames;
            bool reaches_boundary = false;
            if (beats_to_boundary > 0.0) {
                const double frames_to_boundary = beats_to_boundary / bps;
                if (frames_to_boundary <= static_cast<double>(remaining_frames)) {
                    segment_frames = std::max<uint32_t>(1u,
                        static_cast<uint32_t>(std::ceil(frames_to_boundary)));
                    segment_frames = std::min(segment_frames, remaining_frames);
                    reaches_boundary = true;
                }
            }
            const double segment_start = clip_playhead_beats_;
            const double segment_end = std::min(
                segment_start + static_cast<double>(segment_frames) * bps,
                loop_enabled ? boundary_end : pat_len);
            emit_notes_for_block(ctx, local_notes, segment_start, segment_end,
                                 frame_cursor, segment_frames,
                                 transpose.int_value(), velocity_scale.value);
            clip_playhead_beats_ = segment_end;
            frame_cursor += segment_frames;

            if (reaches_boundary || clip_playhead_beats_ >= (loop_enabled ? boundary_end : pat_len) - 1e-9) {
                const uint32_t boundary_frame = frame_cursor == 0 ? 0 :
                    std::min(frame_cursor - 1, ctx->buffer_size - 1);
                stop_all_active_notes(boundary_frame);
                if (loop_enabled) {
                    clip_playhead_beats_ = boundary_start;
                    continue;
                }
                clip_finished_ = true;
                break;
            }
        }
    }

    // Persist pattern playhead for cross-recompile position restoration.
    {
        float ph_f = static_cast<float>(clip_playhead_beats_);
        uint32_t raw = 0;
        std::memcpy(&raw, &ph_f, sizeof(uint32_t));
        pattern_playhead_raw_.store(raw, std::memory_order_relaxed);
    }

    // Publish note buffer
    if (ctx->custom_outputs && ctx->custom_output_count > 0)
        ctx->custom_outputs[0] = &notes_buf_;

    // Phase output: 0..1 through the loop window
    const double phase_origin = loop_enabled ? boundary_start : 0.0;
    const double phase_len = loop_enabled ? loop_len : pat_len;
    const double phase_pos = clip_finished_
        ? phase_len
        : std::clamp(clip_playhead_beats_ - phase_origin, 0.0, phase_len);
    const float phase = (phase_len > 0.0)
        ? static_cast<float>(std::clamp(phase_pos / phase_len, 0.0, 1.0))
        : 0.0f;
    if (ctx->output_buffers && ctx->output_buffers[0]) {
        for (uint32_t i = 0; i < ctx->buffer_size; ++i)
            ctx->output_buffers[0][i] = phase;
    }
}

inline void MidiClipCore::commit_editor_notes(VividEditorContext* ctx) {
    mark_editor_note_order_dirty();
    std::string s = midi_clip::serialize_pattern(editor_notes_);
    static constexpr size_t kInlineNoteLimit = 512;
    static constexpr size_t kInlineByteLimit = 64 * 1024;
    const bool use_sidecar =
        !file.str_value.empty() ||
        !clip_data_ref_.str_value.empty() ||
        editor_notes_.size() > kInlineNoteLimit ||
        s.size() > kInlineByteLimit;

    if (use_sidecar) {
        const std::string ref = !clip_data_ref_.str_value.empty()
            ? clip_data_ref_.str_value
            : default_clip_data_ref_path();
        std::string write_error;
        if (!write_clip_data_ref(ref, editor_notes_, &write_error)) {
            import_status_ = "Clip save failed: " + write_error;
            import_status_until_ = ctx ? ctx->time + 4.0 : 0.0;
            return;
        }

        editor_submitted_str_ = "[]";
        cached_pattern_str_ = "[]";
        cached_clip_data_ref_ = ref;
        clip_data_ref_.str_value = ref;
        clip_data_error_.clear();
        clip_data_note_count_ = editor_notes_.size();
        clip_data_beat_length_ = editor_notes_beat_length();
        set_clip_length_from_beats(clip_data_beat_length_);
        if (clip_data_source_file_.empty())
            clip_data_source_file_ = file.str_value;
        {
            std::lock_guard<std::mutex> lock(pattern_mutex_);
            audio_notes_ = editor_notes_;
            imported_audio_notes_.clear();
        }
        if (ctx && ctx->commands.set_string_param) {
            ctx->commands.set_string_param(ctx->commands.opaque, "clip_data_ref", ref.c_str());
            ctx->commands.set_string_param(ctx->commands.opaque, "pattern_data", "[]");
            if (!file.str_value.empty())
                ctx->commands.set_string_param(ctx->commands.opaque, "file", "");
        }
        file.str_value.clear();
        last_file_path_.clear();
        file_loaded_ = false;
        file_note_count_ = 0;
        file_duration_seconds_ = 0.0;
        SequenceData* old = sequence_.exchange(nullptr, std::memory_order_acq_rel);
        delete deferred_delete_;
        deferred_delete_ = old;
        sequence_generation_.fetch_add(1, std::memory_order_acq_rel);
        clip_generation_.fetch_add(1, std::memory_order_acq_rel);
        return;
    }

    editor_submitted_str_ = s;
    cached_pattern_str_ = s;
    cached_clip_data_ref_.clear();
    clip_data_ref_.str_value.clear();
    {
        std::lock_guard<std::mutex> lock(pattern_mutex_);
        audio_notes_ = editor_notes_;
        imported_audio_notes_.clear();
    }
    if (ctx->commands.set_string_param) {
        ctx->commands.set_string_param(ctx->commands.opaque, "pattern_data", s.c_str());
        ctx->commands.set_string_param(ctx->commands.opaque, "clip_data_ref", "");
        if (!file.str_value.empty())
            ctx->commands.set_string_param(ctx->commands.opaque, "file", "");
    }
    file.str_value.clear();
    last_file_path_.clear();
    set_clip_length_from_beats(static_cast<double>(length_bars.value) * 4.0);
    clip_generation_.fetch_add(1, std::memory_order_acq_rel);
}
