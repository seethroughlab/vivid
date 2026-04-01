// MicInput — Live audio capture operator using miniaudio in capture mode.
//
// Three-thread design:
//   1. Capture callback (miniaudio thread): writes interleaved stereo to SPSC ring
//   2. Audio engine thread (process_audio): reads ring, de-interleaves, applies gain/mute
//   3. Main thread (main_thread_update): owns ma_context + ma_device lifecycle

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "operator_api/operator.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// SPSC ring buffer — capture callback writes, process_audio reads
// ---------------------------------------------------------------------------
struct CaptureRing {
    static constexpr uint32_t kRingSize = 32768; // ~341ms @ 48kHz stereo interleaved
    float ring[kRingSize];
    std::atomic<uint64_t> write_pos{0}; // capture callback writes
    std::atomic<uint64_t> read_pos{0};  // process_audio reads

    void reset() {
        write_pos.store(0, std::memory_order_relaxed);
        read_pos.store(0, std::memory_order_relaxed);
        std::memset(ring, 0, sizeof(ring));
    }

    // Called from capture callback — write interleaved stereo frames
    void write(const float* data, uint32_t frame_count) {
        uint32_t sample_count = frame_count * 2; // stereo interleaved
        uint64_t wp = write_pos.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < sample_count; ++i) {
            ring[(wp + i) % kRingSize] = data[i];
        }
        write_pos.store(wp + sample_count, std::memory_order_release);
    }

    // Called from audio thread — read interleaved stereo samples
    // Returns number of stereo frames actually read
    uint32_t read(float* out, uint32_t max_frames) {
        uint64_t wp = write_pos.load(std::memory_order_acquire);
        uint64_t rp = read_pos.load(std::memory_order_relaxed);
        uint64_t available_samples = wp - rp;
        uint32_t max_samples = max_frames * 2;
        uint32_t to_read = static_cast<uint32_t>(
            available_samples < max_samples ? available_samples : max_samples);
        // Round down to stereo frame boundary
        to_read &= ~1u;
        for (uint32_t i = 0; i < to_read; ++i) {
            out[i] = ring[(rp + i) % kRingSize];
        }
        read_pos.store(rp + to_read, std::memory_order_release);
        return to_read / 2; // return frame count
    }
};

/**
 * @brief Live microphone capture with device selection and metering.
 *
 * Captures audio from a system input device using a triple-buffered
 * ring buffer. Outputs stereo audio plus RMS and peak metering signals.
 *
 * @see WebcamIn, AudioAnalysis
 */
struct MicInput : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "MicInput";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   device{"device", 0, 0, 0};
    vivid::Param<float> gain  {"gain",   1.0f, 0.0f, 4.0f};
    vivid::Param<int>   mute  {"mute",   0, {"Off", "On"}};

    // Dynamic device name storage
    std::vector<std::string>  device_names_;
    std::vector<const char*>  device_name_ptrs_;

    MicInput() {
        vivid::semantic_tag(gain, "amplitude_linear");
        vivid::semantic_shape(gain, "scalar");
        vivid::description(gain, "Input gain multiplier (1 = unity)");

        vivid::semantic_tag(device, "index");
        vivid::semantic_shape(device, "int");
        vivid::description(device, "System audio input device to capture from");

        vivid::description(mute, "Silence the output without closing the device");

        // Enumerate capture devices for dropdown labels
        ma_context ctx;
        ma_result res = ma_context_init(nullptr, 0, nullptr, &ctx);
        if (res == MA_SUCCESS) {
            ma_device_info* capture_infos = nullptr;
            ma_uint32 capture_count = 0;
            res = ma_context_get_devices(&ctx, nullptr, nullptr,
                                         &capture_infos, &capture_count);
            if (res == MA_SUCCESS && capture_count > 0) {
                for (ma_uint32 i = 0; i < capture_count; ++i)
                    device_names_.push_back(capture_infos[i].name);
            }
            ma_context_uninit(&ctx);
        }

        if (device_names_.empty())
            device_names_.push_back("No input device");

        device_name_ptrs_.reserve(device_names_.size());
        for (auto& n : device_names_)
            device_name_ptrs_.push_back(n.c_str());
        device.choice_labels = device_name_ptrs_.data();
        device.choice_count  = static_cast<uint32_t>(device_name_ptrs_.size());
        device.max_value     = static_cast<float>(device_name_ptrs_.size() - 1);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&device);
        out.push_back(&gain);
        out.push_back(&mute);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f});
        out.push_back({"rms",     VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"peak",    VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"gain_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 1.0f});
        vivid::append_analysis_ports(out);
    }

    // -----------------------------------------------------------------------
    // Audio thread — read from ring, de-interleave, apply gain, compute meters
    // -----------------------------------------------------------------------
    void process_audio(const VividAudioContext* ctx) override {
        float* out_l = ctx->output_buffers[0];                   // planar L
        float* out_r = ctx->output_buffers[0] + ctx->buffer_size; // planar R (stereo: ch1 after ch0)
        uint32_t N = ctx->buffer_size;

        bool is_muted = mute.int_value() != 0;
        float gain_cv_val = ctx->input_float_values ? ctx->input_float_values[0] : 1.0f;
        float effective_gain = is_muted ? 0.0f : gain.value * gain_cv_val;

        if (!device_active_.load(std::memory_order_acquire)) {
            // No active capture device — output silence
            std::memset(out_l, 0, N * sizeof(float));
            std::memset(out_r, 0, N * sizeof(float));
            ctx->output_float_values[0] = rms_;
            ctx->output_float_values[1] = peak_;
            return;
        }

        // Read interleaved stereo from ring into scratch buffer
        uint32_t frames_read = ring_.read(interleaved_scratch_, N);

        // De-interleave and apply gain
        float sum_sq = 0.0f;
        float peak_raw = 0.0f;
        uint32_t i = 0;
        for (; i < frames_read; ++i) {
            float l = interleaved_scratch_[i * 2 + 0] * effective_gain;
            float r = interleaved_scratch_[i * 2 + 1] * effective_gain;
            out_l[i] = l;
            out_r[i] = r;
            float mono = (l + r) * 0.5f;
            sum_sq += mono * mono;
            float a = std::fabs(mono);
            if (a > peak_raw) peak_raw = a;
        }
        // Zero-fill any underrun
        if (i < N) {
            std::memset(out_l + i, 0, (N - i) * sizeof(float));
            std::memset(out_r + i, 0, (N - i) * sizeof(float));
        }

        // Smoothed RMS/peak metering
        float alpha = 0.9f;
        float rms_raw = (frames_read > 0) ? std::sqrt(sum_sq / frames_read) : 0.0f;
        rms_  = alpha * rms_  + (1.0f - alpha) * rms_raw;
        peak_ = alpha * peak_ + (1.0f - alpha) * peak_raw;

        ctx->output_float_values[0] = rms_;
        ctx->output_float_values[1] = peak_;
    }

    // -----------------------------------------------------------------------
    // Main thread — device lifecycle management
    // -----------------------------------------------------------------------
    void main_thread_update(double /*time*/) override {
        int desired = device.int_value();

        // Lazy-init ma_context
        if (!context_initialized_) {
            ma_result result = ma_context_init(nullptr, 0, nullptr, &context_);
            if (result != MA_SUCCESS) {
                fprintf(stderr, "[MicInput] Failed to init ma_context: %d\n", result);
                return;
            }
            context_initialized_ = true;
        }

        // Detect device change
        if (desired == current_device_ && device_opened_) return;

        // Stop old device if running
        if (device_opened_) {
            device_active_.store(false, std::memory_order_release);
            ma_device_uninit(&capture_device_);
            device_opened_ = false;
            fprintf(stderr, "[MicInput] Closed previous capture device\n");
        }

        // Enumerate devices
        ma_device_info* capture_infos = nullptr;
        ma_uint32 capture_count = 0;
        ma_result res = ma_context_get_devices(&context_, nullptr, nullptr,
                                                &capture_infos, &capture_count);
        if (res != MA_SUCCESS) {
            fprintf(stderr, "[MicInput] Failed to enumerate devices: %d\n", res);
            return;
        }

        fprintf(stderr, "[MicInput] Available capture devices (%u):\n", capture_count);
        for (ma_uint32 i = 0; i < capture_count; ++i) {
            fprintf(stderr, "  [%u] %s\n", i, capture_infos[i].name);
        }

        if (capture_count == 0) {
            fprintf(stderr, "[MicInput] No capture devices found\n");
            return;
        }

        ma_uint32 dev_idx = static_cast<ma_uint32>(desired);
        if (dev_idx >= capture_count) {
            fprintf(stderr, "[MicInput] Device index %d out of range (max %u), using 0\n",
                    desired, capture_count - 1);
            dev_idx = 0;
        }

        // Reset ring buffer before opening new device
        ring_.reset();

        // Configure and open capture device
        ma_device_config config = ma_device_config_init(ma_device_type_capture);
        config.capture.pDeviceID = &capture_infos[dev_idx].id;
        config.capture.format    = ma_format_f32;
        config.capture.channels  = 2;
        config.sampleRate        = 48000;
        config.dataCallback      = capture_callback;
        config.pUserData         = this;

        res = ma_device_init(&context_, &config, &capture_device_);
        if (res != MA_SUCCESS) {
            fprintf(stderr, "[MicInput] Failed to open capture device '%s': %d\n",
                    capture_infos[dev_idx].name, res);
            if (res == MA_ACCESS_DENIED) {
                fprintf(stderr, "[MicInput] Microphone access denied. "
                        "Grant permission in System Settings > Privacy & Security > Microphone.\n");
            }
            return;
        }

        res = ma_device_start(&capture_device_);
        if (res != MA_SUCCESS) {
            fprintf(stderr, "[MicInput] Failed to start capture device: %d\n", res);
            ma_device_uninit(&capture_device_);
            return;
        }

        device_opened_ = true;
        current_device_ = desired;
        device_active_.store(true, std::memory_order_release);
        fprintf(stderr, "[MicInput] Opened capture device [%u]: %s\n",
                dev_idx, capture_infos[dev_idx].name);
    }

    ~MicInput() override {
        device_active_.store(false, std::memory_order_release);
        if (device_opened_) {
            ma_device_uninit(&capture_device_);
        }
        if (context_initialized_) {
            ma_context_uninit(&context_);
        }
    }

private:
    // Ring buffer
    CaptureRing ring_;
    float interleaved_scratch_[8192]; // enough for buffer_size up to 4096 stereo frames

    // Metering state (audio thread only)
    float rms_  = 0.0f;
    float peak_ = 0.0f;

    // Device state (main thread only, except device_active_ which is shared)
    ma_context context_{};
    ma_device  capture_device_{};
    bool context_initialized_ = false;
    bool device_opened_ = false;
    int  current_device_ = -1;
    std::atomic<bool> device_active_{false};

    // miniaudio capture callback — runs on miniaudio's internal thread
    static void capture_callback(ma_device* device, void* /*output*/,
                                  const void* input, ma_uint32 frame_count) {
        auto* self = static_cast<MicInput*>(device->pUserData);
        if (input && self->device_active_.load(std::memory_order_acquire)) {
            self->ring_.write(static_cast<const float*>(input), frame_count);
        }
    }
};

VIVID_REGISTER(MicInput)
