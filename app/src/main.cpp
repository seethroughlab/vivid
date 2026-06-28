// Vivid PoC — Step 1 foundation: window + WebGPU (Classic GpuContext) +
// miniaudio device + shared transport + test tone.
//
// Proves the shared shell: a window clears each frame (its colour pulses on the
// beat), while the audio thread produces a test tone and advances the master
// transport that the frame thread reads. Half A (VST3/MIDI/session) and Half B
// (shader/analysis) build on this spine.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <cstdio>
#include <cmath>

#include "gpu/gpu_context.h"
#include "gpu/gpu_util.h"
#include "transport.h"
#include "audio/vst3_host.h"
#include "miniaudio.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

struct AudioState {
    Transport* transport = nullptr;
    vivid_poc::Vst3Player* player = nullptr;  // hosted instrument (or null -> test tone)
    double phase = 0.0;       // test-tone oscillator phase
    double tone_hz = 110.0;   // low A
};

// Real-time audio callback: render the hosted instrument's arpeggio (or a test
// tone if no plugin), advance the transport, and publish a block RMS level.
void audio_callback(ma_device* device, void* out, const void* /*in*/, ma_uint32 frames) {
    auto* st = static_cast<AudioState*>(device->pUserData);
    auto* fout = static_cast<float*>(out);
    const double sr = device->sampleRate;

    const double beats = st->transport ? st->transport->beats.load(std::memory_order_relaxed) : 0.0;
    const double bpm   = st->transport ? st->transport->bpm.load(std::memory_order_relaxed) : 120.0;

    bool rendered = false;
    if (st->player)
        rendered = vivid_poc::vst3_player_process(st->player, fout, frames,
                                                  static_cast<uint32_t>(sr), bpm, beats, 4);
    if (!rendered) {
        const double inc = 2.0 * kPi * st->tone_hz / sr;
        for (ma_uint32 i = 0; i < frames; ++i) {
            float s = 0.05f * static_cast<float>(std::sin(st->phase));
            st->phase += inc;
            if (st->phase > 2.0 * kPi) st->phase -= 2.0 * kPi;
            fout[i * 2 + 0] = s;
            fout[i * 2 + 1] = s;
        }
    }

    if (st->transport) {
        st->transport->advance(frames, sr);
        double sum_sq = 0.0;
        for (ma_uint32 i = 0; i < frames; ++i)
            sum_sq += static_cast<double>(fout[i * 2]) * fout[i * 2];
        st->transport->level.store(
            static_cast<float>(std::sqrt(sum_sq / (frames > 0 ? frames : 1))),
            std::memory_order_relaxed);
    }
}

// Record a single render pass that clears `view` to (r,g,b).
void clear_pass(WGPUCommandEncoder encoder, WGPUTextureView view, float r, float g, float b) {
    WGPURenderPassColorAttachment color{};
    color.view = view;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;  // required for 2D attachments in v29
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = WGPUColor{ r, g, b, 1.0 };

    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

}  // namespace

int main() {
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // WebGPU owns the surface
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Vivid PoC — foundation", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "glfwCreateWindow failed\n"); glfwTerminate(); return 1; }

    vivid::GpuContext gpu;
    if (!gpu.init(window, 1280, 800)) {
        std::fprintf(stderr, "GpuContext init failed: %s\n", gpu.last_error().c_str());
        return 1;
    }

    Transport transport;
    AudioState audio_state{};
    audio_state.transport = &transport;  // player set after the device opens

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 0;  // device default
    cfg.dataCallback = audio_callback;
    cfg.pUserData = &audio_state;

    ma_device device;
    bool audio_ok = (ma_device_init(nullptr, &cfg, &device) == MA_SUCCESS);
    if (audio_ok) {
        // Now that we know the device sample rate, scan + load an instrument.
        audio_state.player = vivid_poc::vst3_player_create(device.sampleRate);
        std::fprintf(stderr, "[vivid] instrument: %s\n",
                     audio_state.player ? vivid_poc::vst3_player_name(audio_state.player)
                                        : "none — falling back to test tone");
        if (ma_device_start(&device) != MA_SUCCESS) audio_ok = false;
    }
    std::fprintf(stderr, "[vivid] audio: %s (%u Hz)\n",
                 audio_ok ? "running" : "unavailable", audio_ok ? device.sampleRate : 0);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        const double beats = transport.beats.load(std::memory_order_relaxed);
        const float level = transport.level.load(std::memory_order_relaxed);
        // Background pulses on the beat; a hint of the live audio level rides on top.
        const float pulse = 0.10f + 0.10f * static_cast<float>(0.5 + 0.5 * std::cos(beats * 2.0 * kPi));
        const float b = pulse + level * 2.0f;

        vivid::FrameState frame;
        if (gpu.begin_frame(frame)) {
            clear_pass(frame.encoder, frame.view, 0.04f, pulse, b);
            gpu.end_frame(frame);
        }
    }

    if (audio_ok) ma_device_uninit(&device);  // stops the callback first
    if (audio_state.player) vivid_poc::vst3_player_destroy(audio_state.player);
    gpu.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
