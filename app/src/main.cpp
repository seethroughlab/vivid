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
#include "ui/clip_editor.h"
#include "persist.h"
#include "gpu/shader_op.h"
#include "audio/vst3_plugin_window.h"
#include "platform/macos_frame_timer.h"
#include "gpu/effect_op.h"
#include "gpu/render_target.h"
#include "gpu/texture_source.h"
#include "gpu/video_player.h"
#include <dirent.h>
#include <vector>
#include <string>
#include <algorithm>
#include "miniaudio.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// A right-click context menu of a track's audio characteristics (the bridge).
struct CtxMenu { bool open = false; float x = 0, y = 0; int src = -1; };  // src: -1 master, >=0 track

struct AudioState {
    Transport* transport = nullptr;
    vivid_poc::Session* session = nullptr;  // hosted instrument + clips (or null -> test tone)
    vivid::ui::NodeGraph* graph = nullptr;  // visuals node editor (UI thread)
    CtxMenu menu;                           // characteristic picker (UI thread)
    int gain_drag = -1;                     // mixer gain slider being dragged (UI thread)
    bool split_drag = false;                // dragging the DAW|visuals splitter
    Vst3PluginWindow* track_win[8] = {};    // open plugin editor windows, per track
    vivid::ui::ClipEditor* editor = nullptr;       // MIDI piano-roll (UI thread)
    double last_clip_t = -1; int last_clip_track = -1, last_clip_scene = -1;  // double-click detect
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
constexpr float kTrackX0 = 78.f, kTrackW = 102.f, kTrackGap = 4.f;
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
// Explicit "send this source to the visuals graph" buttons (the bridge entry point).
inline Rect track_viz_rect(int t, int scenes)  { return { track_x(t) + 8.f, mixer_y(scenes) + 48.f, kTrackW - 16.f, 18.f }; }
inline Rect master_viz_rect(int scenes) { return { kSceneColX, mixer_y(scenes) + 48.f, kSceneColW, 18.f }; }
inline void track_accent(int t, float& r, float& g, float& b) {
    static const float P[3][3] = { {0.94f,0.63f,0.19f}, {0.88f,0.39f,0.23f}, {0.35f,0.66f,0.90f} };
    r = P[t%3][0]; g = P[t%3][1]; b = P[t%3][2];
}
// Right/left-click the MASTER meter opens a menu of audio characteristics (the bridge).
struct CharItem { const char* label; int id; };
constexpr CharItem kChars[] = { { "Level (RMS)", 0 }, { "Transient", 1 } };
constexpr int kNumChars = 2;
// Characteristic id encoding: master uses kind (0/1); track t uses 100 + t*2 + kind.
inline int char_id_for(int src, int kind) { return src < 0 ? kind : 100 + src * 2 + kind; }

// Visuals FBO internal resolution (fixed; the on-screen viewer scales to it).
constexpr float kViewW = 720.f, kViewH = 300.f;
// Resizable shell: live window size + the draggable DAW|visuals splitter x.
static int   g_win_w = 1280, g_win_h = 800;
static float g_split_x = 512.f;
inline Rect viewer_rect()   { return { g_split_x + 8.f, 100.f, static_cast<float>(g_win_w) - g_split_x - 16.f, 330.f }; }
inline Rect splitter_rect() { return { g_split_x - 3.f, 44.f, 6.f, static_cast<float>(g_win_h) - 44.f }; }

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

// FBO effect passes (P12b). Vulkan-GLSL separate texture/sampler bindings.
// Feedback: max(gen, zoom(prev) * decay) — outward echoing trails.
static const char* kFeedbackGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U { vec2 u_res; float u_time; float u_decay; float p1; float p2; float p3; };
layout(set = 0, binding = 1) uniform texture2D u_gen;
layout(set = 0, binding = 2) uniform sampler   u_samp;
layout(set = 0, binding = 3) uniform texture2D u_prev;
void main() {
    vec2 c = v_uv - 0.5;
    vec2 puv = 0.5 + c * 0.985;   // slight zoom -> trail drifts outward
    vec4 gen  = texture(sampler2D(u_gen,  u_samp), v_uv);
    vec4 prev = texture(sampler2D(u_prev, u_samp), puv);
    o_color = max(gen, prev * u_decay);
}
)";

// Blur: 5-tap cross, radius scaled by u_radius.
static const char* kBlurGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U { vec2 u_res; float u_time; float u_radius; float p1; float p2; float p3; };
layout(set = 0, binding = 1) uniform texture2D u_tex;
layout(set = 0, binding = 2) uniform sampler   u_samp;
void main() {
    vec2 px = (1.0 / u_res) * (1.0 + u_radius * 8.0);
    vec4 s = texture(sampler2D(u_tex, u_samp), v_uv) * 0.36;
    s += texture(sampler2D(u_tex, u_samp), v_uv + vec2( px.x, 0.0)) * 0.16;
    s += texture(sampler2D(u_tex, u_samp), v_uv + vec2(-px.x, 0.0)) * 0.16;
    s += texture(sampler2D(u_tex, u_samp), v_uv + vec2(0.0,  px.y)) * 0.16;
    s += texture(sampler2D(u_tex, u_samp), v_uv + vec2(0.0, -px.y)) * 0.16;
    o_color = s;
}
)";

// Passthrough blit: sample a source texture (image/video) into the chain.
static const char* kBlitGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U { vec2 u_res; float u_time; float p0; float p1; float p2; float p3; };
layout(set = 0, binding = 1) uniform texture2D u_tex;
layout(set = 0, binding = 2) uniform sampler   u_samp;
void main() { o_color = texture(sampler2D(u_tex, u_samp), v_uv); }
)";

// Visuals generator source: 0 = plasma shader, 1 = texture (image/video). UI thread.
static int g_visual_source = 0;

// Video source: a folder of clips, cycled with N. (UI/main thread only.)
static VideoPlayer*             g_video = nullptr;
static std::vector<std::string> g_video_paths;
static int                      g_video_idx = -1;
static void load_video_at(int i) {
    if (g_video_paths.empty()) return;
    const int n = static_cast<int>(g_video_paths.size());
    g_video_idx = ((i % n) + n) % n;
    if (g_video) { video_close(g_video); g_video = nullptr; }
    g_video = video_open(g_video_paths[g_video_idx].c_str());
    if (g_video) {
        video_play(g_video, g_visual_source == 1);
        std::fprintf(stderr, "[vivid] video [%d/%d]: %s\n", g_video_idx + 1, n, g_video_paths[g_video_idx].c_str());
    }
}

// The Session view on Renderer2D: transport, a tracks×scenes clip grid, a mixer.
void draw_ui(vivid::ui::Renderer2D& ui, const AudioState& st, double beats, double mx, double my) {
    ui.draw_rect(0, 0, static_cast<float>(g_win_w), 40, 0.07f, 0.08f, 0.10f, 1.0f);
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

    ui.push_clip_rect(0.f, 40.f, g_split_x, static_cast<float>(g_win_h) - 40.f);  // DAW pane
    ui.draw_rect(0.f, 40.f, g_split_x, static_cast<float>(g_win_h) - 40.f, 0.065f, 0.072f, 0.085f, 1.0f);  // pane bg
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
            char cn[16];
            const int abpm = vivid_poc::session_track_is_audio(s, t) ? vivid_poc::session_audio_clip_bpm(s, t, sc) : 0;
            if (abpm > 0) std::snprintf(cn, sizeof cn, "%d BPM", abpm);
            else          std::snprintf(cn, sizeof cn, "Clip %c", 'A' + sc);
            ui.draw_text(r.x + 36.f, r.y + 17.f, cn, on ? 0.9f : 0.6f, on ? 0.93f : 0.64f, 1.0f, 0.95f);
        }
    }
    // mixer
    const float my0 = mixer_y(scenes);
    ui.draw_text(kSceneColX, my0, "MIX", 0.45f, 0.48f, 0.53f, 1.0f, 0.82f);
    // A teal "+ VIZ" button = send this source into the visuals graph as a node.
    auto viz_button = [&](const Rect& b, bool small) {
        const bool h = hit(b, mx, my);
        ui.draw_rounded_rect(b.x, b.y, b.w, b.h, 4.f, h ? 0.16f : 0.11f, h ? 0.26f : 0.18f, h ? 0.27f : 0.20f, 1.0f);
        ui.draw_rect(b.x, b.y, 3.f, b.h, 0.31f, 0.80f, 0.75f, 1.0f);
        ui.draw_text(b.x + (small ? 8.f : 10.f), b.y + 4.f, small ? "+VIZ" : "+ VIZ",
                     0.55f, 0.85f, 0.82f, 1.0f, 0.82f);
    };
    for (int t = 0; t < tracks; ++t) {
        const Rect mr = track_meter_rect(t, scenes), gr = track_gain_rect(t, scenes);
        const float lvl = std::min(1.0f, vivid_poc::session_track_level(s, t) * 4.0f);
        ui.draw_rect(mr.x, mr.y, mr.w, mr.h, 0.07f, 0.08f, 0.10f, 1.0f);
        ui.draw_rect(mr.x, mr.y, mr.w * lvl, mr.h, 0.30f, 0.80f, 0.50f, 1.0f);
        const float g = vivid_poc::session_track_gain(s, t);
        ui.draw_rect(gr.x, gr.y, gr.w, gr.h, 0.10f, 0.11f, 0.13f, 1.0f);
        ui.draw_rect(gr.x, gr.y, gr.w * g, gr.h, 0.32f, 0.46f, 0.66f, 1.0f);
        ui.draw_rect(gr.x + gr.w * g - 2.f, gr.y - 2.f, 4.f, gr.h + 4.f, 0.7f, 0.8f, 0.95f, 1.0f);
        viz_button(track_viz_rect(t, scenes), false);
    }
    // master meter + its viz button
    const Rect mm = master_meter_rect(scenes);
    const float ml = st.transport ? std::min(1.0f, st.transport->level.load(std::memory_order_relaxed) * 4.0f) : 0.f;
    ui.draw_rect(mm.x, mm.y, mm.w, mm.h, 0.07f, 0.08f, 0.10f, 1.0f);
    ui.draw_rect(mm.x, mm.y, mm.w * ml, mm.h, 0.31f, 0.80f, 0.75f, 1.0f);
    ui.draw_text(kSceneColX, mm.y + 8.f, "MASTER", 0.85f, 0.9f, 0.92f, 1.0f, 0.78f);
    viz_button(master_viz_rect(scenes), true);
    ui.draw_text(kSceneColX, my0 + 78.f,
                 "+VIZ \xE2\x86\x92 add a node to the visuals graph (then drag its port onto a shader input)",
                 0.42f, 0.55f, 0.56f, 1.0f, 0.85f);

    ui.pop_clip_rect();  // end DAW pane
    // Visuals pane label (clipped to the right pane)
    const Rect vp = viewer_rect();
    ui.push_clip_rect(g_split_x, 40.f, static_cast<float>(g_win_w) - g_split_x, static_cast<float>(g_win_h) - 40.f);
    ui.draw_text(vp.x, 80.f, g_visual_source ? "VISUALS — video source  ·  V: plasma  ·  N: next clip"
                                             : "VISUALS — plasma shader  ·  V: video",
                 0.55f, 0.78f, 0.85f, 1.0f, 0.95f);
    ui.pop_clip_rect();
    // DAW | visuals splitter (on top, unclipped)
    const Rect sp = splitter_rect();
    const bool sph = hit(sp, mx, my);
    ui.draw_rect(sp.x, sp.y, sp.w, sp.h, sph ? 0.30f : 0.16f, sph ? 0.34f : 0.17f, sph ? 0.40f : 0.20f, 1.0f);
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
void key_callback(GLFWwindow* w, int key, int /*sc*/, int action, int mods) {
    if (action != GLFW_PRESS) return;
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (!st) return;
    if (key == GLFW_KEY_ESCAPE && st->editor && st->editor->is_open()) { st->editor->close(); return; }

    // Cmd+S / Cmd+O -> save / load the session (~/vivid_session.json).
    if ((mods & GLFW_MOD_SUPER) && st->session && st->graph && (key == GLFW_KEY_S || key == GLFW_KEY_O)) {
        const char* home = std::getenv("HOME");
        const std::string path = std::string(home ? home : ".") + "/vivid_session.json";
        if (key == GLFW_KEY_S) {
            const bool ok = vivid::save_session(path, st->session, *st->graph, g_win_w, g_win_h, g_split_x);
            std::fprintf(stderr, "[vivid] save %s: %s\n", path.c_str(), ok ? "ok" : "FAILED");
        } else {
            int ww = g_win_w, wh = g_win_h; float sxx = g_split_x;
            const bool ok = vivid::load_session(path, st->session, *st->graph, ww, wh, sxx);
            if (ok) { g_split_x = sxx; glfwSetWindowSize(w, ww, wh); }
            std::fprintf(stderr, "[vivid] load %s: %s\n", path.c_str(), ok ? "ok" : "FAILED");
        }
        return;
    }
    if (key == GLFW_KEY_V) {  // toggle the visuals generator source
        g_visual_source ^= 1;
        if (g_video) video_play(g_video, g_visual_source == 1);
        std::fprintf(stderr, "[vivid] visual source: %s\n", g_visual_source ? "texture/video" : "plasma");
        return;
    }
    if (key == GLFW_KEY_N) { load_video_at(g_video_idx + 1); return; }  // next clip
    if (!st->session) return;
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
        int idx = key - GLFW_KEY_1;
        if (idx < vivid_poc::session_scene_count(st->session)) {
            vivid_poc::session_launch_scene(st->session, idx);
            std::fprintf(stderr, "[vivid] launch scene %c (queued for next bar)\n", 'A' + idx);
        }
    }
}

void scroll_callback(GLFWwindow* w, double /*xoff*/, double yoff) {
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (!st || !st->editor || !st->editor->is_open()) return;
    double mx, my; glfwGetCursorPos(w, &mx, &my);
    if (st->editor->contains(mx, my)) st->editor->scroll(yoff);
}

// Which visuals source is under (mx,my): -1 = master, >=0 = track, -2 = none.
// The +VIZ button (or its meter) is the click target.
int meter_hit(int tracks, int scenes, double mx, double my) {
    if (hit(master_viz_rect(scenes), mx, my) || hit(master_meter_rect(scenes), mx, my)) return -1;
    for (int t = 0; t < tracks; ++t)
        if (hit(track_viz_rect(t, scenes), mx, my) || hit(track_meter_rect(t, scenes), mx, my)) return t;
    return -2;
}

void mouse_button_callback(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (!st) return;
    double mx, my; glfwGetCursorPos(w, &mx, &my);
    const int tracks = st->session ? vivid_poc::session_track_count(st->session) : 0;
    const int scenes = st->session ? vivid_poc::session_scene_count(st->session) : 0;

    // Clip editor is non-modal: route presses inside its panel to it; clicks
    // elsewhere pass through to the session. A release always ends any editor drag.
    if (st->editor && st->editor->is_open()) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) st->editor->on_up(mx, my);
        if (action == GLFW_PRESS && st->editor->contains(mx, my)) {
            if (button == GLFW_MOUSE_BUTTON_LEFT) st->editor->on_down(mx, my, glfwGetTime());
            return;
        }
    }

    // DAW | visuals splitter.
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) st->split_drag = false;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && hit(splitter_rect(), mx, my)) {
        st->split_drag = true; return;
    }

    // Right-click a meter (master or per-track) -> open its characteristic menu.
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        const int src = st->session ? meter_hit(tracks, scenes, mx, my) : -2;
        if (src != -2) st->menu = { true, static_cast<float>(mx), static_cast<float>(my), src };
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
                const int src = st->menu.src;
                const char* sname = src < 0 ? "Master" : vivid_poc::session_track_name(st->session, src);
                std::string title = std::string(sname) + "  " + kChars[j].label;
                st->graph->add_data_node(title, char_id_for(src, kChars[j].id));
                std::fprintf(stderr, "[vivid] bridge: spawned '%s %s' node\n", sname, kChars[j].label);
                break;
            }
        }
        st->menu.open = false;
        return;
    }
    if (!st->session) return;

    // A meter (master or per-track) -> open its characteristic menu (left-click).
    {
        const int src = meter_hit(tracks, scenes, mx, my);
        if (src != -2) { st->menu = { true, static_cast<float>(mx), static_cast<float>(my), src }; return; }
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
    // clip cells -> single click launches; double click opens the MIDI editor
    for (int t = 0; t < tracks; ++t)
        for (int sc = 0; sc < scenes; ++sc)
            if (hit(clip_cell_rect(t, sc), mx, my)) {
                const double now = glfwGetTime();
                if (st->editor && st->last_clip_track == t && st->last_clip_scene == sc && now - st->last_clip_t < 0.35) {
                    char title[80];
                    std::snprintf(title, sizeof title, "%s  \xC2\xB7  Clip %c",
                                  vivid_poc::session_track_name(st->session, t), 'A' + sc);
                    if (vivid_poc::session_track_is_audio(st->session, t)) {  // waveform editor
                        float bins[512]; float a = 0.f, b = 1.f;
                        const int nb = vivid_poc::session_audio_waveform(st->session, t, sc, bins, 512);
                        vivid_poc::session_get_audio_trim(st->session, t, sc, &a, &b);
                        st->editor->open_audio(t, sc, title, bins, nb, a, b);
                    } else {                                                  // piano-roll editor
                        vivid_poc::ClipNote buf[256];
                        const int n = vivid_poc::session_get_clip(st->session, t, sc, buf, 256);
                        const double len = vivid_poc::session_clip_length(st->session, t, sc);
                        st->editor->open(t, sc, title, buf, n, len);
                    }
                    st->last_clip_t = -1;
                    return;
                }
                st->last_clip_t = now; st->last_clip_track = t; st->last_clip_scene = sc;
                vivid_poc::session_launch_clip(st->session, t, sc);
                return;
            }
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

    // FBO effect chain: plasma -> genRT -> feedback (ping-pong fb[2]) -> blur -> screen.
    const uint32_t kRtW = static_cast<uint32_t>(kViewW), kRtH = static_cast<uint32_t>(kViewH);
    vivid::RenderTarget genRT, fb[2];
    genRT.init(gpu.device(), kRtW, kRtH, gpu.surface_format());
    fb[0].init(gpu.device(), kRtW, kRtH, gpu.surface_format());
    fb[1].init(gpu.device(), kRtW, kRtH, gpu.surface_format());
    int fbCur = 0;
    vivid::EffectOp feedbackOp, blurOp, blitOp;
    feedbackOp.init(gpu.device(), gpu.queue(), gpu.surface_format(), kFeedbackGLSL, 2);
    blurOp.init(gpu.device(), gpu.queue(), gpu.surface_format(), kBlurGLSL, 1);
    blitOp.init(gpu.device(), gpu.queue(), gpu.surface_format(), kBlitGLSL, 1);

    // Texture source (image/video) — seeded with a test pattern; P19b feeds video.
    vivid::TextureSource srcTex;
    srcTex.init(gpu.device(), 512, 288, gpu.surface_format());
    { auto pat = vivid::gen_test_pattern(512, 288); srcTex.upload(gpu.queue(), pat.data()); }

    // Scan the video folder and open the first clip (N cycles, V shows it).
    {
        const char* dir = "/Users/jeff/Movies/Gero Individual Reel Files";
        if (DIR* d = opendir(dir)) {
            while (dirent* e = readdir(d)) {
                std::string n = e->d_name;
                if (n.size() > 4 && n.compare(n.size() - 4, 4, ".mp4") == 0)
                    g_video_paths.push_back(std::string(dir) + "/" + n);
            }
            closedir(d);
            std::sort(g_video_paths.begin(), g_video_paths.end());
        }
        if (!g_video_paths.empty()) load_video_at(0);
        std::fprintf(stderr, "[vivid] %zu video clips found\n", g_video_paths.size());
    }

    vivid::ui::NodeGraph graph;
    vivid::ui::ClipEditor clip_editor;

    Transport transport;
    AudioState audio_state{};
    audio_state.transport = &transport;  // player set after the device opens
    audio_state.graph = &graph;
    audio_state.editor = &clip_editor;

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
    glfwSetScrollCallback(window, scroll_callback);
    std::fprintf(stderr, "[vivid] audio: %s (%u Hz)\n",
                 audio_ok ? "running" : "unavailable", audio_ok ? device.sampleRate : 0);

    float react = 0.f;   // smoothed master level
    float trHold = 0.f;  // peak-held transient (so onsets are visible at frame rate)
    float trkReact[8] = {0}, trkTrHold[8] = {0};  // per-track smoothed level / held transient

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

        // Resizable shell: reconfigure the surface when the window changes size.
        { int ww = 0, wh = 0; glfwGetWindowSize(window, &ww, &wh);
          if (ww > 0 && wh > 0 && (static_cast<uint32_t>(ww) != gpu.width() || static_cast<uint32_t>(wh) != gpu.height())) {
              gpu.resize(static_cast<uint32_t>(ww), static_cast<uint32_t>(wh));
              g_win_w = ww; g_win_h = wh;
          }
          g_split_x = std::min(g_split_x, static_cast<float>(g_win_w) - 220.f);
          clip_editor.set_window(static_cast<float>(g_win_w), static_cast<float>(g_win_h));
        }

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
        graph.set_value(0, react);                 // master level
        graph.set_value(1, std::min(1.0f, trHold)); // master transient
        for (int t = 0; audio_state.session && t < vivid_poc::session_track_count(audio_state.session) && t < 8; ++t) {
            const float lv = vivid_poc::session_track_level(audio_state.session, t);
            trkReact[t] += (std::min(1.0f, lv * 5.0f) - trkReact[t]) * 0.3f;
            trkTrHold[t] *= 0.85f;
            trkTrHold[t] = std::max(trkTrHold[t], vivid_poc::session_track_transient(audio_state.session, t));
            graph.set_value(char_id_for(t, 0), trkReact[t]);
            graph.set_value(char_id_for(t, 1), std::min(1.0f, trkTrHold[t]));
        }
        float uniforms[vivid::kNumShaderUniforms];
        graph.fill_uniforms(uniforms);             // per-uniform wired values (0 if unwired)

        double mx, my; glfwGetCursorPos(window, &mx, &my);
        if (audio_state.split_drag)  // continue a splitter drag
            g_split_x = std::clamp(static_cast<float>(mx), 500.f, static_cast<float>(g_win_w) - 220.f);
        graph.on_move(mx, my);  // continue any node/wire drag
        if (clip_editor.is_open()) {           // continue a drag, commit edits
            clip_editor.on_move(mx, my);
            if (clip_editor.take_dirty() && audio_state.session) {
                if (clip_editor.is_audio()) {
                    float a, b; clip_editor.audio_trim(a, b);
                    vivid_poc::session_set_audio_trim(audio_state.session, clip_editor.track(), clip_editor.scene(), a, b);
                } else {
                    const auto& nv = clip_editor.notes();
                    vivid_poc::session_set_clip(audio_state.session, clip_editor.track(), clip_editor.scene(),
                                                nv.data(), static_cast<int>(nv.size()), clip_editor.length());
                }
            }
        }
        if (audio_state.gain_drag >= 0 && audio_state.session) {  // continue a mixer gain drag
            const Rect gr = track_gain_rect(audio_state.gain_drag, vivid_poc::session_scene_count(audio_state.session));
            vivid_poc::session_set_track_gain(audio_state.session, audio_state.gain_drag,
                                              std::min(1.0, std::max(0.0, (mx - gr.x) / gr.w)));
        }

        vivid::FrameState frame;
        if (gpu.begin_frame(frame)) {
            const float tsec = static_cast<float>(glfwGetTime());
            const float rtw = static_cast<float>(kRtW), rth = static_cast<float>(kRtH);
            // pull the latest video frame into the source texture (if showing video)
            if (g_visual_source == 1 && g_video) {
                const uint8_t* px = nullptr; uint32_t vw = 0, vh = 0;
                if (video_next_frame(g_video, &px, &vw, &vh) && px) {
                    if (vw != srcTex.w || vh != srcTex.h) {
                        srcTex.release();
                        srcTex.init(gpu.device(), vw, vh, gpu.surface_format());
                    }
                    srcTex.upload(gpu.queue(), px);
                }
            }
            // 1) generator -> genRT: plasma shader (ports 0..3) or the texture source
            if (g_visual_source == 1) {
                const WGPUTextureView srcIn[1] = { srcTex.view };
                blitOp.render(frame.encoder, genRT.view, 0, 0, rtw, rth, /*clear*/true, srcIn, 1, tsec, nullptr, 0);
            } else {
                shader.render(frame.encoder, genRT.view, 0, 0, rtw, rth, tsec, uniforms, /*clear*/true);
            }
            // 2) feedback: genRT + previous history -> current history (port 4 = decay)
            vivid::RenderTarget& cur = fb[fbCur];
            vivid::RenderTarget& prev = fb[fbCur ^ 1];
            const WGPUTextureView fbIn[2] = { genRT.view, prev.view };
            const float decay = 0.82f + uniforms[4] * 0.16f;
            feedbackOp.render(frame.encoder, cur.view, 0, 0, rtw, rth, /*clear*/true,
                              fbIn, 2, tsec, &decay, 1);
            // 3) blur current history -> screen viewport (port 5 = radius)
            clear_pass(frame.encoder, frame.view, 0.045f, 0.05f, 0.06f);  // static dark backdrop
            const WGPUTextureView blIn[1] = { cur.view };
            const float radius = uniforms[5];
            const Rect vp = viewer_rect();
            blurOp.render(frame.encoder, frame.view, vp.x, vp.y, vp.w, vp.h, /*clear*/false,
                          blIn, 1, tsec, &radius, 1);
            fbCur ^= 1;

            draw_ui(ui, audio_state, beats, mx, my);
            const Rect vrp = viewer_rect();
            graph.set_bounds(g_split_x + 8.f, vrp.y + vrp.h + 16.f,
                             static_cast<float>(g_win_w) - 8.f, static_cast<float>(g_win_h) - 8.f);
            graph.draw(ui);
            draw_menu(ui, audio_state.menu,
                      audio_state.menu.src < 0 ? "Master"
                      : (audio_state.session ? vivid_poc::session_track_name(audio_state.session, audio_state.menu.src) : "track"));
            clip_editor.draw(ui);  // modal overlay on top
            ui.flush(frame.encoder, frame.view, g_win_w, g_win_h, g_win_w, g_win_h);
            gpu.end_frame(frame);
        }
        return true;
    };
    vivid::macos_run_frame_loop(poll_events, tick);

    if (audio_ok) ma_device_uninit(&device);  // stops the callback first
    for (int t = 0; t < 8; ++t) if (audio_state.track_win[t]) vst3_plugin_window_close(audio_state.track_win[t]);
    if (audio_state.session) vivid_poc::session_destroy(audio_state.session);
    if (g_video) { video_close(g_video); g_video = nullptr; }
    feedbackOp.shutdown(); blurOp.shutdown(); blitOp.shutdown();
    srcTex.release();
    genRT.release(); fb[0].release(); fb[1].release();
    shader.shutdown();
    ui.shutdown();
    gpu.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
