#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "avf_audio_extractor.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

// =============================================================================
// MovieFileAudioIn — extracts audio from a movie file and outputs L/R channels.
//
// Sync strategy:
//   MovieFileIn (GPU) outputs a "time" control float → connected to video_time param.
//   main_thread_update() checks drift between video_time and audio read head PTS.
//   - <50ms: tolerate (normal jitter)
//   - 50ms–500ms: skip/pad handled by audio thread (fine correction)
//   - >500ms: resync AVAssetReader from main thread
//
// Threading:
//   extractor_ is created/destroyed on main thread (main_thread_update).
//   Audio thread only calls read_samples()/read_head_pts() on it.
//   We use a raw atomic pointer to make the handoff safe:
//   main thread stores new extractor, audio thread loads it.
// =============================================================================

struct MovieFileAudioIn : vivid::OperatorBase {
    static constexpr const char* kName   = "MovieFileAudioIn";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::FilePath> file {"file"};
    vivid::Param<float> volume      {"volume", 1.0f, 0.0f, 2.0f};
    vivid::Param<float> video_time  {"video_time", 0.0f, 0.0f, 86400.0f};
    vivid::Param<float> video_speed {"video_speed", 1.0f, 0.0f, 4.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
        out.push_back(&volume);
        out.push_back(&video_time);
        out.push_back(&video_speed);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"left",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"right", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    // Main thread: called via update_sources hook each frame
    void main_thread_update(double /*time*/) override {
        // Deferred delete: by now the audio thread has observed the new pointer
        // (at least one full audio buffer has elapsed since the previous frame).
        delete deferred_delete_;
        deferred_delete_ = nullptr;

        // Check if file path changed
        if (file.str_value != last_path_) {
            last_path_ = file.str_value;

            // Cancel any in-flight async load
            pending_load_.reset();

            // Clear current extractor immediately (audio thread gets silence)
            auto* old = extractor_.load(std::memory_order_relaxed);
            extractor_.store(nullptr, std::memory_order_release);
            deferred_delete_ = old;

            if (!last_path_.empty()) {
                // Launch async load on background thread
                auto result = std::make_shared<AsyncAudioLoad>();
                pending_load_ = result;
                std::string path = last_path_;
                std::thread([result, path]{
                    auto* ext = new AVFAudioExtractor();
                    if (ext->open(path)) {
                        result->extractor = ext;
                        result->success = true;
                    } else {
                        delete ext;
                    }
                    result->done.store(true, std::memory_order_release);
                }).detach();
            }
            return;  // Skip sync check on file change
        }

        // Check if async load completed
        if (pending_load_ && pending_load_->done.load(std::memory_order_acquire)) {
            if (pending_load_->success) {
                auto* old = extractor_.load(std::memory_order_relaxed);
                // Take ownership — null it so AsyncAudioLoad destructor won't delete
                auto* fresh = pending_load_->extractor;
                pending_load_->extractor = nullptr;
                extractor_.store(fresh, std::memory_order_release);
                deferred_delete_ = old;
            }
            pending_load_.reset();
        }

        auto* ext = extractor_.load(std::memory_order_acquire);
        if (!ext || !ext->is_open() || !ext->has_audio()) return;

        // Don't start audio until video is providing a time signal.
        // Without this, the audio loads faster than video and the sync
        // logic outputs silence or resyncs in a loop until video catches up.
        if (video_time.value <= 0.0f) return;

        // Update playback speed for pitch-preserving time stretch
        ext->set_speed(video_speed.value);

        // Check sync drift (coarse correction: resync AVAssetReader)
        double drift = video_time.value - ext->read_head_pts();
        double drift_threshold = 0.5 * std::max(1.0, static_cast<double>(video_speed.value));
        if (std::abs(drift) > drift_threshold) {
            ext->resync(video_time.value);
        }

        // Pre-fill ring buffer from AVAssetReader
        ext->fill_buffer();
    }

    // Audio thread
    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float* L = audio->output_buffers[0];
        float* R = audio->output_buffers[1];
        uint32_t n = audio->buffer_size;

        auto* ext = extractor_.load(std::memory_order_acquire);
        if (ext && ext->is_open() && ext->has_audio()) {
            ext->read_samples(L, R, n);
        } else {
            std::memset(L, 0, n * sizeof(float));
            std::memset(R, 0, n * sizeof(float));
        }

        // Apply volume
        float vol = volume.value;
        for (uint32_t i = 0; i < n; i++) {
            L[i] *= vol;
            R[i] *= vol;
        }
    }

    ~MovieFileAudioIn() override {
        delete deferred_delete_;
        auto* ext = extractor_.load(std::memory_order_relaxed);
        delete ext;
    }

private:
    struct AsyncAudioLoad {
        std::atomic<bool> done{false};
        bool success = false;
        AVFAudioExtractor* extractor = nullptr;
        ~AsyncAudioLoad() { delete extractor; }
    };

    std::atomic<AVFAudioExtractor*> extractor_{nullptr};
    AVFAudioExtractor* deferred_delete_ = nullptr;  // held for one frame before deletion
    std::shared_ptr<AsyncAudioLoad> pending_load_;
    std::string last_path_;
};

VIVID_REGISTER(MovieFileAudioIn)
