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

struct Rect { float x, y, w, h; };
inline bool hit(const Rect& r, double mx, double my) {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}
// Session grid layout: one track column, one clip cell per scene row.
inline Rect clip_cell_rect(int i) {
    const float gx = 48.f, gy = 120.f, cw = 220.f, ch = 52.f, gap = 8.f;
    return { gx, gy + i * (ch + gap), cw, ch };
}

// The Session view on Renderer2D: transport bar, a track column, and clickable
// clip cells (active = blue + play glyph, queued = amber stripe, hover = lighter).
void draw_ui(vivid::ui::Renderer2D& ui, const AudioState& st, double beats, double mx, double my) {
    ui.draw_rect(0, 0, 1280, 40, 0.07f, 0.08f, 0.10f, 1.0f);
    ui.draw_text(20, 12, "VIVID — Session", 0.90f, 0.93f, 0.97f, 1.0f, 1.15f);
    const double bpm = st.transport ? st.transport->bpm.load(std::memory_order_relaxed) : 120.0;
    char tb[64]; std::snprintf(tb, sizeof tb, "%.0f BPM   4/4", bpm);
    ui.draw_text(190, 13, tb, 0.6f, 0.64f, 0.7f, 1.0f, 0.95f);
    int beat = static_cast<int>(std::floor(beats)) % 4; if (beat < 0) beat += 4;
    for (int i = 0; i < 4; ++i) {
        const bool ob = (i == beat);
        ui.draw_rect(360 + i * 22.f, 13, 14, 14, ob ? 0.95f : 0.22f, ob ? 0.7f : 0.24f, ob ? 0.2f : 0.27f, 1.0f);
    }

    const char* name = st.session ? vivid_poc::session_name(st.session) : "—";
    ui.draw_text(48, 80, "TRACK", 0.45f, 0.48f, 0.53f, 1.0f, 0.85f);
    ui.draw_rect(48, 94, 220, 24, 0.13f, 0.14f, 0.17f, 1.0f);
    ui.draw_text(58, 98, name, 0.85f, 0.88f, 0.92f, 1.0f);

    const int active = st.session ? vivid_poc::session_active_clip(st.session) : -1;
    const int queued = st.session ? vivid_poc::session_queued_clip(st.session) : -1;
    const int clips  = st.session ? vivid_poc::session_clip_count(st.session) : 0;
    for (int i = 0; i < clips; ++i) {
        const Rect r = clip_cell_rect(i);
        const bool on = (i == active), q = (i == queued);
        const float br = hit(r, mx, my) ? 0.06f : 0.0f;  // hover lighten
        if (on) ui.draw_rect(r.x, r.y, r.w, r.h, 0.17f + br, 0.28f + br, 0.40f + br, 1.0f);
        else    ui.draw_rect(r.x, r.y, r.w, r.h, 0.13f + br, 0.14f + br, 0.16f + br, 1.0f);
        if (on) ui.draw_rect(r.x, r.y, 3, r.h, 0.40f, 0.70f, 1.0f, 1.0f);        // active accent
        if (q)  ui.draw_rect(r.x, r.y, r.w, 3, 0.95f, 0.75f, 0.20f, 1.0f);       // queued stripe
        if (on) ui.draw_tri(r.x + 16, r.y + 18, r.x + 16, r.y + 34, r.x + 30, r.y + 26, 0.5f, 0.85f, 0.5f, 1.0f);
        char cn[24]; std::snprintf(cn, sizeof cn, "Clip %d", i + 1);
        ui.draw_text(r.x + 40, r.y + 18, cn, on ? 0.92f : 0.66f, on ? 0.95f : 0.70f, 1.0f, 1.0f);
    }
    const float fy = (clips > 0) ? clip_cell_rect(clips - 1).y + 64.f : 130.f;
    ui.draw_text(48, fy, "click a clip to launch  ·  or press 1/2/3", 0.45f, 0.48f, 0.53f, 1.0f, 0.9f);
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

// Left-click a clip cell to launch it (Proof A).
void mouse_button_callback(GLFWwindow* w, int button, int action, int /*mods*/) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return;
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (!st || !st->session) return;
    double mx, my; glfwGetCursorPos(w, &mx, &my);
    const int clips = vivid_poc::session_clip_count(st->session);
    for (int i = 0; i < clips; ++i) {
        if (hit(clip_cell_rect(i), mx, my)) {
            vivid_poc::session_launch(st->session, i);
            std::fprintf(stderr, "[vivid] launch clip %d (click)\n", i + 1);
            break;
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
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    std::fprintf(stderr, "[vivid] audio: %s (%u Hz)\n",
                 audio_ok ? "running" : "unavailable", audio_ok ? device.sampleRate : 0);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        const double beats = transport.beats.load(std::memory_order_relaxed);
        const float level = transport.level.load(std::memory_order_relaxed);
        // Background pulses on the beat; a hint of the live audio level rides on top.
        const float pulse = 0.10f + 0.10f * static_cast<float>(0.5 + 0.5 * std::cos(beats * 2.0 * kPi));
        const float b = pulse + level * 2.0f;

        double mx, my; glfwGetCursorPos(window, &mx, &my);  // for hover

        vivid::FrameState frame;
        if (gpu.begin_frame(frame)) {
            clear_pass(frame.encoder, frame.view, 0.04f, pulse, b);
            draw_ui(ui, audio_state, beats, mx, my);
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
