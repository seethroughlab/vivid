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
#include "ui/node_graph.h"
#include "gpu/shader_op.h"
#include "audio/vst3_plugin_window.h"
#include "platform/macos_frame_timer.h"
#include "miniaudio.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// A right-click context menu of a track's audio characteristics (the bridge).
struct CtxMenu { bool open = false; float x = 0, y = 0; };

struct AudioState {
    Transport* transport = nullptr;
    vivid_poc::Session* session = nullptr;  // hosted instrument + clips (or null -> test tone)
    vivid::ui::NodeGraph* graph = nullptr;  // visuals node editor (UI thread)
    CtxMenu menu;                           // characteristic picker (UI thread)
    int gain_drag = -1;                     // mixer gain slider being dragged (UI thread)
    Vst3PluginWindow* track_win[8] = {};    // open plugin editor windows, per track
    float tr_baseline = 0.f;                // onset detector baseline (audio thread)
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
        const float rms = static_cast<float>(std::sqrt(sum_sq / (frames > 0 ? frames : 1)));
        st->transport->level.store(rms, std::memory_order_relaxed);
        // transient = how far RMS jumps above its slow baseline (a simple onset detector)
        const float tr = std::max(0.0f, (rms - st->tr_baseline) * 6.0f);
        st->tr_baseline += (rms - st->tr_baseline) * 0.04f;
        st->transport->transient.store(std::min(1.0f, tr), std::memory_order_relaxed);
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
// Session grid: columns = tracks, rows = scenes; a mixer strip below.
constexpr float kSceneColX = 14.f, kSceneColW = 58.f;
constexpr float kTrackX0 = 78.f, kTrackW = 132.f, kTrackGap = 4.f;
constexpr float kHeaderY = 56.f, kHeaderH = 30.f;
constexpr float kGridTopY = 92.f;
constexpr float kRowH = 50.f, kRowGap = 4.f;
inline float track_x(int t) { return kTrackX0 + t * (kTrackW + kTrackGap); }
inline Rect clip_cell_rect(int track, int scene) { return { track_x(track), kGridTopY + scene * (kRowH + kRowGap), kTrackW, kRowH }; }
inline Rect track_header_rect(int t) { return { track_x(t), kHeaderY, kTrackW, kHeaderH }; }
inline Rect scene_launch_rect(int scene) { return { kSceneColX, kGridTopY + scene * (kRowH + kRowGap), kSceneColW, kRowH }; }
inline float mixer_y(int scenes) { return kGridTopY + scenes * (kRowH + kRowGap) + 18.f; }
inline Rect track_meter_rect(int t, int scenes) { return { track_x(t) + 8.f, mixer_y(scenes) + 16.f, kTrackW - 16.f, 8.f }; }
inline Rect track_gain_rect(int t, int scenes)  { return { track_x(t) + 8.f, mixer_y(scenes) + 30.f, kTrackW - 16.f, 12.f }; }
inline Rect master_meter_rect(int scenes) { return { kSceneColX, mixer_y(scenes) + 16.f, kSceneColW, 26.f }; }
inline void track_accent(int t, float& r, float& g, float& b) {
    static const float P[3][3] = { {0.94f,0.63f,0.19f}, {0.88f,0.39f,0.23f}, {0.35f,0.66f,0.90f} };
    r = P[t%3][0]; g = P[t%3][1]; b = P[t%3][2];
}
// Right/left-click the MASTER meter opens a menu of audio characteristics (the bridge).
struct CharItem { const char* label; int id; };
constexpr CharItem kChars[] = { { "Level (RMS)", 0 }, { "Transient", 1 } };
constexpr int kNumChars = 2;

// Viewer pane (right) where the visuals shader op renders.
constexpr float kViewX = 512.f, kViewY = 120.f, kViewW = 720.f, kViewH = 300.f;

// The visual: a GLSL fragment shader, compiled natively by wgpu-native (no glslang).
// Named uniforms (P11): warp = domain distortion, hue = colour rotation,
// density = pattern frequency, glow = brightness. Each is a wireable input port.
static const char* kFragGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U {
    vec2 u_res; float u_time; float u_warp; float u_hue; float u_density; float u_glow; };
void main() {
    vec2 uv = v_uv;
    float t = u_time;
    float dens = 6.0 + u_density * 18.0;
    vec2 w = uv + u_warp * 0.3 * vec2(sin(uv.y * 8.0 + t), cos(uv.x * 8.0 + t));
    float v = sin(w.x * dens + t) + sin(w.y * dens + t * 1.3)
            + sin((w.x + w.y) * dens * 0.6 + t * 0.7)
            + sin(length(w - 0.5) * dens * 1.8 - t * 2.0);
    vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + v + u_hue * 6.2832);
    o_color = vec4(col * (0.6 + u_glow), 1.0);
}
)";

// The Session view on Renderer2D: transport, a tracks×scenes clip grid, a mixer.
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
    if (!st.session) return;
    auto* s = st.session;
    const int tracks = vivid_poc::session_track_count(s);
    const int scenes = vivid_poc::session_scene_count(s);

    // track headers
    for (int t = 0; t < tracks; ++t) {
        const Rect h = track_header_rect(t);
        float ar, ag, ab; track_accent(t, ar, ag, ab);
        const float hb = hit(h, mx, my) ? 0.04f : 0.f;  // hover: clickable -> editor
        ui.draw_rect(h.x, h.y, h.w, h.h, 0.12f + hb, 0.13f + hb, 0.16f + hb, 1.0f);
        ui.draw_rect(h.x, h.y, h.w, 3.f, ar, ag, ab, 1.0f);
        char nm[40]; std::snprintf(nm, sizeof nm, "%.18s", vivid_poc::session_track_name(s, t));
        ui.draw_text(h.x + 8.f, h.y + 9.f, nm, 0.85f, 0.88f, 0.92f, 1.0f, 0.95f);
    }
    // scene rows + clip cells
    for (int sc = 0; sc < scenes; ++sc) {
        const Rect sb = scene_launch_rect(sc);
        ui.draw_rect(sb.x, sb.y, sb.w, sb.h, hit(sb, mx, my) ? 0.17f : 0.12f, 0.13f, 0.15f, 1.0f);
        char sl[4]; std::snprintf(sl, sizeof sl, "%c", 'A' + sc);
        ui.draw_text(sb.x + 10.f, sb.y + 8.f, sl, 0.8f, 0.82f, 0.86f, 1.0f);
        ui.draw_tri(sb.x + 12.f, sb.y + 30.f, sb.x + 12.f, sb.y + 42.f, sb.x + 23.f, sb.y + 36.f, 0.55f, 0.6f, 0.66f, 1.0f);
        for (int t = 0; t < tracks; ++t) {
            const Rect r = clip_cell_rect(t, sc);
            const bool on = vivid_poc::session_active_clip(s, t) == sc;
            const bool q  = vivid_poc::session_queued_clip(s, t) == sc;
            const float br = hit(r, mx, my) ? 0.05f : 0.f;
            float ar, ag, ab; track_accent(t, ar, ag, ab);
            if (on) ui.draw_rect(r.x, r.y, r.w, r.h, 0.16f + br, 0.20f + br, 0.26f + br, 1.0f);
            else    ui.draw_rect(r.x, r.y, r.w, r.h, 0.115f + br, 0.125f + br, 0.145f + br, 1.0f);
            if (on) ui.draw_rect(r.x, r.y, 3.f, r.h, ar, ag, ab, 1.0f);
            if (q)  ui.draw_rect(r.x, r.y, r.w, 3.f, 0.95f, 0.75f, 0.20f, 1.0f);
            if (on) ui.draw_tri(r.x + 14.f, r.y + 18.f, r.x + 14.f, r.y + 32.f, r.x + 27.f, r.y + 25.f, 0.5f, 0.85f, 0.5f, 1.0f);
            char cn[16]; std::snprintf(cn, sizeof cn, "Clip %c", 'A' + sc);
            ui.draw_text(r.x + 36.f, r.y + 17.f, cn, on ? 0.9f : 0.6f, on ? 0.93f : 0.64f, 1.0f, 0.95f);
        }
    }
    // mixer
    const float my0 = mixer_y(scenes);
    ui.draw_text(kSceneColX, my0, "MIX", 0.45f, 0.48f, 0.53f, 1.0f, 0.82f);
    for (int t = 0; t < tracks; ++t) {
        const Rect mr = track_meter_rect(t, scenes), gr = track_gain_rect(t, scenes);
        const float lvl = std::min(1.0f, vivid_poc::session_track_level(s, t) * 4.0f);
        ui.draw_rect(mr.x, mr.y, mr.w, mr.h, 0.07f, 0.08f, 0.10f, 1.0f);
        ui.draw_rect(mr.x, mr.y, mr.w * lvl, mr.h, 0.30f, 0.80f, 0.50f, 1.0f);
        const float g = vivid_poc::session_track_gain(s, t);
        ui.draw_rect(gr.x, gr.y, gr.w, gr.h, 0.10f, 0.11f, 0.13f, 1.0f);
        ui.draw_rect(gr.x, gr.y, gr.w * g, gr.h, 0.32f, 0.46f, 0.66f, 1.0f);
        ui.draw_rect(gr.x + gr.w * g - 2.f, gr.y - 2.f, 4.f, gr.h + 4.f, 0.7f, 0.8f, 0.95f, 1.0f);
    }
    // master meter — the bridge entry point
    const Rect mm = master_meter_rect(scenes);
    const float ml = st.transport ? std::min(1.0f, st.transport->level.load(std::memory_order_relaxed) * 4.0f) : 0.f;
    ui.draw_rect(mm.x, mm.y, mm.w, mm.h, 0.07f, 0.08f, 0.10f, 1.0f);
    ui.draw_rect(mm.x, mm.y, mm.w * ml, mm.h, 0.31f, 0.80f, 0.75f, 1.0f);
    ui.draw_text(kSceneColX, mm.y + mm.h + 6.f, "MASTER", 0.55f, 0.78f, 0.85f, 1.0f, 0.8f);
    ui.draw_text(kSceneColX, mm.y + mm.h + 22.f, "click \xE2\x86\x92 visuals", 0.45f, 0.62f, 0.66f, 1.0f, 0.78f);
    ui.draw_text(kSceneColX, my0 + 78.f, "click a clip \xC2\xB7 A/B/C = launch row \xC2\xB7 click a track name \xE2\x86\x92 plugin editor", 0.42f, 0.45f, 0.5f, 1.0f, 0.85f);

    ui.draw_text(kViewX, 96, "VISUALS — GLSL shader op", 0.55f, 0.78f, 0.85f, 1.0f, 0.95f);
}

// The characteristic context menu (the bridge entry point).
void draw_menu(vivid::ui::Renderer2D& ui, const CtxMenu& m, const char* track) {
    if (!m.open) return;
    const float w = 184.f;
    char hdr[96]; std::snprintf(hdr, sizeof hdr, "%s  →  visuals", track && *track ? track : "track");
    ui.draw_rect(m.x, m.y - 22.f, w, 22.f, 0.09f, 0.10f, 0.12f, 1.0f);
    ui.draw_text(m.x + 10.f, m.y - 18.f, hdr, 0.55f, 0.58f, 0.64f, 1.0f, 0.82f);
    for (int j = 0; j < kNumChars; ++j) {
        const float iy = m.y + j * 26.f;
        ui.draw_rect(m.x, iy, w, 26.f, 0.16f, 0.17f, 0.20f, 1.0f);
        ui.draw_rect(m.x, iy, 3.f, 26.f, 0.31f, 0.80f, 0.75f, 1.0f);
        ui.draw_text(m.x + 14.f, iy + 6.f, kChars[j].label, 0.85f, 0.88f, 0.92f, 1.0f);
    }
}

// Number keys 1..N launch scene 0..N-1 across all tracks (applied on the next bar).
void key_callback(GLFWwindow* w, int key, int /*sc*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (!st || !st->session) return;
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
        int idx = key - GLFW_KEY_1;
        if (idx < vivid_poc::session_scene_count(st->session)) {
            vivid_poc::session_launch_scene(st->session, idx);
            std::fprintf(stderr, "[vivid] launch scene %c (queued for next bar)\n", 'A' + idx);
        }
    }
}

void mouse_button_callback(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (!st) return;
    double mx, my; glfwGetCursorPos(w, &mx, &my);
    const int tracks = st->session ? vivid_poc::session_track_count(st->session) : 0;
    const int scenes = st->session ? vivid_poc::session_scene_count(st->session) : 0;

    // Right-click the MASTER meter -> open the characteristic menu (the bridge).
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        if (st->session && hit(master_meter_rect(scenes), mx, my))
            st->menu = { true, static_cast<float>(mx), static_cast<float>(my) };
        else st->menu.open = false;
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    if (action == GLFW_RELEASE) { st->gain_drag = -1; if (st->graph) st->graph->on_up(mx, my); return; }
    if (action != GLFW_PRESS) return;

    // Menu has priority: pick a characteristic -> spawn a data node in the graph.
    if (st->menu.open) {
        for (int j = 0; j < kNumChars; ++j) {
            const Rect r = { st->menu.x, st->menu.y + j * 26.f, 184.f, 26.f };
            if (hit(r, mx, my) && st->graph) {
                std::string title = std::string("Master  ") + kChars[j].label;
                st->graph->add_data_node(title, kChars[j].id);
                std::fprintf(stderr, "[vivid] bridge: spawned '%s' node\n", kChars[j].label);
                break;
            }
        }
        st->menu.open = false;
        return;
    }
    if (!st->session) return;

    // MASTER meter -> open the characteristic menu (left-click; right-click also works).
    if (hit(master_meter_rect(scenes), mx, my)) {
        st->menu = { true, static_cast<float>(mx), static_cast<float>(my) };
        return;
    }
    // Click a track header -> open that track's plugin editor (change presets there).
    for (int t = 0; t < tracks; ++t) {
        if (hit(track_header_rect(t), mx, my)) {
            auto* ctrl = static_cast<Steinberg::Vst::IEditController*>(vivid_poc::session_track_controller(st->session, t));
            if (ctrl) {
                if (st->track_win[t]) { vst3_plugin_window_close(st->track_win[t]); st->track_win[t] = nullptr; }
                st->track_win[t] = vst3_plugin_window_open(ctrl, vivid_poc::session_track_name(st->session, t));
                std::fprintf(stderr, "[vivid] track %d editor: %s\n", t, st->track_win[t] ? "opened" : "no GUI");
            }
            return;
        }
    }
    // mixer gain sliders
    for (int t = 0; t < tracks; ++t) {
        const Rect gr = track_gain_rect(t, scenes);
        if (hit(gr, mx, my)) {
            st->gain_drag = t;
            vivid_poc::session_set_track_gain(st->session, t, std::min(1.0, std::max(0.0, (mx - gr.x) / gr.w)));
            return;
        }
    }
    if (st->graph && st->graph->on_down(mx, my)) return;  // node graph consumed it
    // clip cells -> launch a single clip
    for (int t = 0; t < tracks; ++t)
        for (int sc = 0; sc < scenes; ++sc)
            if (hit(clip_cell_rect(t, sc), mx, my)) { vivid_poc::session_launch_clip(st->session, t, sc); return; }
    // scene launch buttons -> launch the whole row
    for (int sc = 0; sc < scenes; ++sc)
        if (hit(scene_launch_rect(sc), mx, my)) { vivid_poc::session_launch_scene(st->session, sc); return; }
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

    vivid::ShaderOp shader;
    if (!shader.init(gpu.device(), gpu.queue(), gpu.surface_format(), kFragGLSL))
        std::fprintf(stderr, "[vivid] shader op init failed (viewer disabled)\n");

    vivid::ui::NodeGraph graph;

    Transport transport;
    AudioState audio_state{};
    audio_state.transport = &transport;  // player set after the device opens
    audio_state.graph = &graph;

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
        std::fprintf(stderr, "[vivid] session: %d tracks (track 0: %s)\n",
                     audio_state.session ? vivid_poc::session_track_count(audio_state.session) : 0,
                     audio_state.session ? vivid_poc::session_track_name(audio_state.session, 0) : "none — test tone");
        if (ma_device_start(&device) != MA_SUCCESS) audio_ok = false;
    }
    glfwSetWindowUserPointer(window, &audio_state);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    std::fprintf(stderr, "[vivid] audio: %s (%u Hz)\n",
                 audio_ok ? "running" : "unavailable", audio_ok ? device.sampleRate : 0);

    float react = 0.f;   // smoothed master level
    float trHold = 0.f;  // peak-held transient (so onsets are visible at frame rate)

    // Event polling is split from rendering and driven by a CFRunLoopTimer (see
    // macos_run_frame_loop): rendering must keep firing while macOS runs a nested
    // tracking run-loop (the one a hosted plugin GUI enters on mouse-down), and a
    // plain glfwPollEvents busy-loop never services that nested loop — so plugin
    // editor windows render but ignore clicks. The timer fires in tracking mode too.
    auto poll_events = [&]() -> bool {
        glfwPollEvents();
        return !glfwWindowShouldClose(window);
    };
    auto tick = [&]() -> bool {
        if (glfwWindowShouldClose(window)) return false;

        // reap plugin editor windows the user closed
        for (int t = 0; audio_state.session && t < vivid_poc::session_track_count(audio_state.session); ++t)
            if (audio_state.track_win[t] && !vst3_plugin_window_is_open(audio_state.track_win[t])) {
                vst3_plugin_window_close(audio_state.track_win[t]); audio_state.track_win[t] = nullptr;
            }

        const double beats = transport.beats.load(std::memory_order_relaxed);
        const float level = transport.level.load(std::memory_order_relaxed);
        react += (std::min(1.0f, level * 5.0f) - react) * 0.3f;            // smoothed level
        trHold *= 0.85f;                                                   // decay the held peak
        trHold = std::max(trHold, transport.transient.load(std::memory_order_relaxed));
        graph.set_value(0, react);                 // characteristic 0: level
        graph.set_value(1, std::min(1.0f, trHold)); // characteristic 1: transient
        float uniforms[vivid::kNumShaderUniforms];
        graph.fill_uniforms(uniforms);             // per-uniform wired values (0 if unwired)

        double mx, my; glfwGetCursorPos(window, &mx, &my);
        graph.on_move(mx, my);  // continue any node/wire drag
        if (audio_state.gain_drag >= 0 && audio_state.session) {  // continue a mixer gain drag
            const Rect gr = track_gain_rect(audio_state.gain_drag, vivid_poc::session_scene_count(audio_state.session));
            vivid_poc::session_set_track_gain(audio_state.session, audio_state.gain_drag,
                                              std::min(1.0, std::max(0.0, (mx - gr.x) / gr.w)));
        }

        vivid::FrameState frame;
        if (gpu.begin_frame(frame)) {
            clear_pass(frame.encoder, frame.view, 0.045f, 0.05f, 0.06f);  // static dark
            shader.render(frame.encoder, frame.view, kViewX, kViewY, kViewW, kViewH,
                          static_cast<float>(glfwGetTime()), uniforms);
            draw_ui(ui, audio_state, beats, mx, my);
            graph.draw(ui);
            draw_menu(ui, audio_state.menu, "Master");
            ui.flush(frame.encoder, frame.view, 1280, 800, 1280, 800);
            gpu.end_frame(frame);
        }
        return true;
    };
    vivid::macos_run_frame_loop(poll_events, tick);

    if (audio_ok) ma_device_uninit(&device);  // stops the callback first
    for (int t = 0; t < 8; ++t) if (audio_state.track_win[t]) vst3_plugin_window_close(audio_state.track_win[t]);
    if (audio_state.session) vivid_poc::session_destroy(audio_state.session);
    shader.shutdown();
    ui.shutdown();
    gpu.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
