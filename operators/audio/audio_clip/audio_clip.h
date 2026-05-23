#pragma once

#include "operator_api/operator.h"
#include "operator_api/metronome_sync.h"
#include "operator_api/editor_ui.h"
#include <atomic>
#include <string>
#include <vector>

// Waveform overview for the editor — lightweight, no audio dependencies.
// Built on the main thread; read only by draw_editor (same thread).
struct AudioClipWaveform {
    static constexpr uint32_t kBins = 8192;
    struct Bin { float min_L, max_L, min_R, max_R; };
    Bin      bins[kBins]{};
    uint32_t actual_bins      = 0;
    double   duration_sec     = 0.0;
    uint32_t frame_count      = 0;
    uint32_t file_sample_rate = 0;
};

struct AudioClip : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName        = "AudioClip";
    static constexpr const char* kDisplayName = "Audio Clip";
    static constexpr const char* kSummary =
        "Linear WAV file player with pitch-preserving time stretch and BPM sync.";

    // ---- Params ----
    vivid::Param<vivid::FilePath>  file       {"file"};
    vivid::Param<int>   auto_play  {"auto_play", 1,    {"manual", "auto"}};
    vivid::Param<int>   loop       {"loop",       1,    {"off", "on"}};
    vivid::Param<float> loop_start {"loop_start", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> loop_end   {"loop_end",   1.0f, 0.0f, 1.0f};
    vivid::Param<float> volume     {"volume",     1.0f, 0.0f, 2.0f};
    vivid::Param<float> speed      {"speed",      1.0f, 0.1f, 8.0f};
    vivid::Param<float> pitch      {"pitch",      0.0f, -24.0f, 24.0f};
    vivid::Param<float> file_bpm   {"file_bpm",   0.0f, 0.0f, 300.0f};
    vivid::Param<int>   rate_mode  {"rate_mode",  vivid::kRateModeFree,
                                    vivid::rate_mode_labels()};
    vivid::Param<int>   stretch    {"stretch",    1, {"off", "on"}};
    vivid::Param<float> clip_start {"clip_start", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> clip_end   {"clip_end",   1.0f, 0.0f, 1.0f};
    vivid::Param<vivid::TextValue> warp_points {"warp_points"};  // reserved v2

    AudioClip() {
        vivid::description(file,       "WAV file to play");
        vivid::description(auto_play,  "auto: start playing as soon as a file loads; manual: wait for a rising edge on the play port");
        vivid::description(loop,       "Loop playback when the end of the loop region is reached");
        vivid::description(loop_start, "Normalized start of the loop region (0–1), constrained inside the clip region");
        vivid::description(loop_end,   "Normalized end of the loop region (0–1), constrained inside the clip region");
        vivid::description(volume,     "Output gain (0–2)");
        vivid::description(speed,      "Playback speed multiplier (free rate mode only)");
        vivid::description(pitch,      "Pitch shift in semitones, independent of speed (stretch=on only)");
        vivid::description(file_bpm,   "Native BPM of the source file; required for rate_mode=metronome");
        vivid::description(rate_mode,  "free: use speed param  |  external: beat_phase port scrubs position per-sample  |  metronome: sync to graph metronome via file_bpm");
        vivid::description(stretch,    "on: pitch-preserving time stretch  |  off: tape/vinyl (pitch follows speed)");
        vivid::description(clip_start, "Normalized source input boundary (0–1); playback starts here");
        vivid::description(clip_end,   "Normalized source output boundary (0–1); non-looping playback ends here");
    }

    // ---- Waveform display data (main-thread only) ----
    // Points into the current ClipState's embedded waveform. Null when no file is loaded.
    const AudioClipWaveform* display_waveform_ = nullptr;

    // ---- Editor interaction state (main-thread only) ----
    vivid::ui::DragHandleState clip_start_drag_{};
    vivid::ui::DragHandleState clip_end_drag_{};
    vivid::ui::DragHandleState loop_start_drag_{};
    vivid::ui::DragHandleState loop_end_drag_{};
    vivid::ui::DragHandleState loop_body_drag_{};
    float clip_start_orig_   = 0.0f;
    float clip_end_orig_     = 1.0f;
    float loop_start_orig_   = 0.0f;
    float loop_end_orig_     = 1.0f;
    float loop_body_ls_orig_ = 0.0f;
    float loop_body_le_orig_ = 1.0f;
    vivid::ui::SliderState side_sliders_[10]{};
    vivid::ui::Viewport1D  timeline_vp_{};
    bool   timeline_vp_init_ = false;

    // ---- Lifecycle ----
    ~AudioClip() override;
    void collect_params(std::vector<vivid::ParamBase*>& out) override;
    void collect_ports(std::vector<VividPortDescriptor>& out) override;
    void main_thread_update(double time) override;
    void process_audio(const VividAudioContext* ctx) override;

    // ---- Editor ----
    static VividEditorMetadata editor_metadata();
    void draw_editor(VividEditorContext* ctx);

private:
    struct ClipState;  // fully defined in audio_clip.cpp

    std::atomic<ClipState*> clip_{nullptr};
    ClipState*              deferred_delete_ = nullptr;
    ClipState*              last_clip_seen_  = nullptr;
    std::string             last_path_;
    std::atomic<uint32_t>   known_sr_{0};
    float                   detected_bpm_    = 0.0f;  // main-thread only; from last loaded file
    double                  prev_sync_phase_ = -1.0;  // previous sync loop phase; detects wrap for stretcher reset

    // Audio thread state
    bool     is_playing_        = false;
    double   playback_pos_      = 0.0;
    float    prev_play_         = 0.0f;
    float    prev_stop_         = 0.0f;
    bool     done_pulse_        = false;
    float    last_pitch_        = 999.0f;  // sentinel — forces setTransposeSemitones on first use
    uint32_t drain_frames_left_ = 0;       // non-zero while flushing stretcher tail after non-loop end

    static void build_waveform_bins(ClipState* s);

    // cs=clip_start, ce=clip_end frame positions; ls/le=loop region (constrained inside clip)
    bool gather_source(ClipState* state, uint32_t cs, uint32_t ce,
                       uint32_t ls, uint32_t le, bool p_loop, uint32_t count);
    bool simple_render(ClipState* state, float* out_L, float* out_R,
                       uint32_t N, uint32_t cs, uint32_t ce,
                       uint32_t ls, uint32_t le, bool p_loop, float advance,
                       float* pos_out);
    void emit_scalars(const VividAudioContext* ctx, uint32_t N,
                      float position, float done);
};
