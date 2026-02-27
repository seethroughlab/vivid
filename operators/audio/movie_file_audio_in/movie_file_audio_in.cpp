#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "avf_audio_extractor.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

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
    vivid::Param<float> volume     {"volume", 1.0f, 0.0f, 2.0f};
    vivid::Param<float> video_time {"video_time", 0.0f, 0.0f, 86400.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
        out.push_back(&volume);
        out.push_back(&video_time);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"left",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"right", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    // Main thread: called via update_sources hook each frame
    void main_thread_update(double /*time*/) override {
        // Check if file path changed
        if (file.str_value != last_path_) {
            last_path_ = file.str_value;

            // Replace extractor (audio thread will pick up via atomic)
            auto* old = extractor_.load(std::memory_order_relaxed);
            if (last_path_.empty()) {
                extractor_.store(nullptr, std::memory_order_release);
            } else {
                auto* fresh = new AVFAudioExtractor();
                if (fresh->open(last_path_)) {
                    extractor_.store(fresh, std::memory_order_release);
                } else {
                    delete fresh;
                    extractor_.store(nullptr, std::memory_order_release);
                }
            }
            delete old;
            return;  // Skip sync check on file change
        }

        auto* ext = extractor_.load(std::memory_order_acquire);
        if (!ext || !ext->is_open() || !ext->has_audio()) return;

        // Check sync drift (coarse correction: resync AVAssetReader)
        double drift = video_time.value - ext->read_head_pts();
        if (std::abs(drift) > 0.5) {
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

            // Fine sync correction
            double drift = video_time.value - ext->read_head_pts();
            if (drift > 0.1) {
                // Audio is behind video — skip a few samples to catch up
                uint32_t skip = std::min(static_cast<uint32_t>(drift * audio->sample_rate * 0.5),
                                         n / 2);
                float tmp_l[256], tmp_r[256];
                while (skip > 0) {
                    uint32_t chunk = std::min(skip, 256u);
                    ext->read_samples(tmp_l, tmp_r, chunk);
                    skip -= chunk;
                }
            } else if (drift < -0.1) {
                // Audio is ahead of video — output silence this buffer, let video catch up
                std::memset(L, 0, n * sizeof(float));
                std::memset(R, 0, n * sizeof(float));
            }
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
        auto* ext = extractor_.load(std::memory_order_relaxed);
        delete ext;
    }

private:
    std::atomic<AVFAudioExtractor*> extractor_{nullptr};
    std::string last_path_;
};

VIVID_REGISTER(MovieFileAudioIn)
