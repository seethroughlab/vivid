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
#include "ui/renderer_2d.h"
#include "miniaudio.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

struct AudioState {
    Transport* transport = nullptr;
    vivid_poc::Session* session = nullptr;  // hosted instrument + clips (or null -> test tone)
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
    if (st->session)
        rendered = vivid_poc::session_process(st->session, fout, frames,
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

// A tiny status HUD on Renderer2D: instrument name, clip launch buttons (active
// highlighted, queued marked), beat dots. Proves text + shapes draw on the GPU.
void draw_ui(vivid::ui::Renderer2D& ui, const AudioState& st, double beats) {
    ui.draw_rect(32, 32, 430, 168, 0.10f, 0.11f, 0.13f, 0.92f);
    ui.draw_text(48, 42, "VIVID — C++ PoC", 0.92f, 0.94f, 0.97f, 1.0f, 1.2f);

    const char* name = st.session ? vivid_poc::session_name(st.session) : "no instrument";
    char line[160];
    std::snprintf(line, sizeof line, "instrument: %s", name);
    ui.draw_text(48, 74, line, 0.62f, 0.66f, 0.72f, 1.0f);

    const int active = st.session ? vivid_poc::session_active_clip(st.session) : -1;
    const int queued = st.session ? vivid_poc::session_queued_clip(st.session) : -1;
    const int clips  = st.session ? vivid_poc::session_clip_count(st.session) : 0;
    for (int i = 0; i < clips; ++i) {
        const float x = 48 + i * 58.0f, y = 102;
        const bool on = (i == active);
        ui.draw_rect(x, y, 50, 38, on ? 0.18f : 0.14f, on ? 0.30f : 0.15f, on ? 0.44f : 0.17f, 1.0f);
        if (i == queued) ui.draw_rect(x, y, 50, 3, 0.95f, 0.75f, 0.20f, 1.0f);  // queued marker
        char n[8]; std::snprintf(n, sizeof n, "%d", i + 1);
        ui.draw_text(x + 20, y + 11, n, on ? 0.95f : 0.6f, on ? 0.97f : 0.64f, 1.0f, 1.0f);
    }
    ui.draw_text(48, 162, "press 1/2/3 to launch a clip", 0.5f, 0.53f, 0.58f, 1.0f, 0.95f);

    // beat dots
    int beat = static_cast<int>(std::floor(beats)) % 4; if (beat < 0) beat += 4;
    for (int i = 0; i < 4; ++i) {
        const bool ob = (i == beat);
        ui.draw_rect(348 + i * 24.0f, 44, 16, 16,
                     ob ? 0.95f : 0.22f, ob ? 0.70f : 0.24f, ob ? 0.20f : 0.27f, 1.0f);
    }
}

// Number keys 1..N launch clip 0..N-1 (applied on the next bar).
void key_callback(GLFWwindow* w, int key, int /*sc*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (!st || !st->session) return;
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
        int idx = key - GLFW_KEY_1;
        if (idx < vivid_poc::session_clip_count(st->session)) {
            vivid_poc::session_launch(st->session, idx);
            std::fprintf(stderr, "[vivid] launch clip %d (queued for next bar)\n", idx + 1);
        }
    }
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

    vivid::ui::Renderer2D ui;
    if (!ui.init(gpu.device(), gpu.surface_format(), VIVID_FONT_PATH, 15.0f))
        std::fprintf(stderr, "[vivid] Renderer2D init failed (UI disabled)\n");

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
        audio_state.session = vivid_poc::session_create(device.sampleRate);
        std::fprintf(stderr, "[vivid] instrument: %s (%d clips — press 1..N to launch)\n",
                     audio_state.session ? vivid_poc::session_name(audio_state.session)
                                         : "none — falling back to test tone",
                     audio_state.session ? vivid_poc::session_clip_count(audio_state.session) : 0);
        if (ma_device_start(&device) != MA_SUCCESS) audio_ok = false;
    }
    glfwSetWindowUserPointer(window, &audio_state);
    glfwSetKeyCallback(window, key_callback);
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
            draw_ui(ui, audio_state, beats);
            ui.flush(frame.encoder, frame.view, 1280, 800, 1280, 800);
            gpu.end_frame(frame);
        }
    }

    if (audio_ok) ma_device_uninit(&device);  // stops the callback first
    if (audio_state.session) vivid_poc::session_destroy(audio_state.session);
    ui.shutdown();
    gpu.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
