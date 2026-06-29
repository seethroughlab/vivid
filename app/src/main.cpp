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
#include "ui/ui_style.h"
#include "ui/clip_editor.h"
#include "persist.h"
#include "gpu/shader_op.h"
#include "audio/vst3_plugin_window.h"
#include "platform/macos_frame_timer.h"
#include "gpu/effect_op.h"
#include "gpu/render_target.h"
#include "gpu/visual_graph.h"
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
    CtxMenu fx_menu;                        // "+ FX" picker (src = track)
    CtxMenu map_menu;                       // param "map from source" picker
    int map_param = -1;                     // param index the map menu targets
    int sel_track = 0;                      // track whose device chain is shown
    int sel_device = 0;                     // device whose params are shown (0=inst, 1+=fx)
    int param_drag = -1;                    // param slider being dragged
    double last_dev_t = -1; int last_dev_i = -1;  // device double-click detect
    int gain_drag = -1;                     // mixer gain slider being dragged (UI thread)
    bool split_drag = false;                // dragging the DAW|visuals splitter
    double split_last_t = -1.0;             // last splitter press (double-click reset)
    Vst3PluginWindow* track_win[8] = {};    // open instrument editor windows, per track
    Vst3PluginWindow* fx_win[8] = {};       // open effect editor windows (pool)
    vivid::ui::ClipEditor* editor = nullptr;       // MIDI piano-roll (UI thread)
    double last_clip_t = -1; int last_clip_track = -1, last_clip_scene = -1;  // double-click detect
    float tr_baseline = 0.f;                // onset detector baseline (audio thread)
    float m_flt_lo = 0.f, m_flt_hi = 0.f;   // master 3-band crossover states (audio thread)
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
        const float a_lo = 1.f - std::exp(-6.2832f * 200.f / static_cast<float>(sr));
        const float a_hi = 1.f - std::exp(-6.2832f * 2000.f / static_cast<float>(sr));
        double sum_sq = 0.0, slo = 0.0, smi = 0.0, shi = 0.0;
        for (ma_uint32 i = 0; i < frames; ++i) {
            const float l = fout[i * 2];
            sum_sq += static_cast<double>(l) * l;
            st->m_flt_lo += (l - st->m_flt_lo) * a_lo;
            st->m_flt_hi += (l - st->m_flt_hi) * a_hi;
            const float lo = st->m_flt_lo, mi = st->m_flt_hi - st->m_flt_lo, hi = l - st->m_flt_hi;
            slo += static_cast<double>(lo) * lo; smi += static_cast<double>(mi) * mi; shi += static_cast<double>(hi) * hi;
        }
        const double inv = 1.0 / (frames > 0 ? frames : 1);
        const float rms = static_cast<float>(std::sqrt(sum_sq * inv));
        st->transport->level.store(rms, std::memory_order_relaxed);
        st->transport->band_low.store(static_cast<float>(std::sqrt(slo * inv)), std::memory_order_relaxed);
        st->transport->band_mid.store(static_cast<float>(std::sqrt(smi * inv)), std::memory_order_relaxed);
        st->transport->band_high.store(static_cast<float>(std::sqrt(shi * inv)), std::memory_order_relaxed);
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
constexpr float kRowH = 56.f, kRowGap = 4.f;
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
// Device chain (P23) for the selected track: instrument + effects + "+ FX".
inline float device_y(int scenes) { return mixer_y(scenes) + 100.f; }
inline Rect  device_box(int i, int scenes) { return { kSceneColX + i * 124.f, device_y(scenes) + 18.f, 116.f, 40.f }; }
inline Rect  device_x_btn(int i, int scenes) { Rect b = device_box(i, scenes); return { b.x + b.w - 17.f, b.y + 3.f, 14.f, 14.f }; }
// Params panel for the selected device (P24): a column of sliders.
constexpr int kMaxShownParams = 12;
inline float params_y(int scenes) { return device_y(scenes) + 78.f; }
inline Rect  param_slider_rect(int i, int scenes) { return { kSceneColX + 110.f, params_y(scenes) + 20.f + i * 22.f, 158.f, 12.f }; }
inline Rect  param_map_btn(int i, int scenes)     { return { kSceneColX + 88.f,  params_y(scenes) + 20.f + i * 22.f, 16.f, 12.f }; }
// Sources offered when mapping an audio param (the return path): audio characteristics + visuals state.
struct MapSrc { const char* label; const char* id; };
constexpr MapSrc kMapSources[] = {
    { "Master Level", "master.level" }, { "Master Transient", "master.transient" },
    { "Master Low", "master.low" }, { "Master Mid", "master.mid" }, { "Master High", "master.high" },
    { "Viz Warp", "viz.warp" }, { "Viz Glow", "viz.glow" }, { "Viz Feedback", "viz.feedback" },
    { "\xE2\x80\x94 clear \xE2\x80\x94", "" } };
constexpr int kNumMapSources = 9;
inline std::string param_dest(int track, int device, int i) {
    return "param:" + std::to_string(track) + ":" + std::to_string(device) + ":" + std::to_string(i);
}
inline void track_accent(int t, float& r, float& g, float& b) {
    static const float P[3][3] = { {0.94f,0.63f,0.19f}, {0.88f,0.39f,0.23f}, {0.35f,0.66f,0.90f} };
    r = P[t%3][0]; g = P[t%3][1]; b = P[t%3][2];
}
// Right/left-click the MASTER meter opens a menu of audio characteristics (the bridge).
struct CharItem { const char* label; int id; };
constexpr CharItem kChars[] = {
    { "Level (RMS)", 0 }, { "Transient", 1 }, { "Low band", 2 }, { "Mid band", 3 }, { "High band", 4 } };
constexpr int kNumChars = 5;
// Characteristic id encoding: master uses kind (0..4); track t uses 100 + t*8 + kind.
inline int char_id_for(int src, int kind) { return src < 0 ? kind : 100 + src * 8 + kind; }

// Visuals FBO internal resolution (fixed; the on-screen viewer scales to it).
constexpr float kViewW = 720.f, kViewH = 300.f;
// Resizable shell: live window size + the draggable DAW|visuals splitter x.
static int   g_win_w = 1280, g_win_h = 800;
static float g_split_x = 512.f;
inline Rect viewer_rect()   { return { g_split_x + 8.f, 100.f, static_cast<float>(g_win_w) - g_split_x - 16.f, 330.f }; }
inline Rect splitter_rect() { return { g_split_x - 3.f, 44.f, 6.f, static_cast<float>(g_win_h) - 44.f }; }

// Visuals generator source: 0 = plasma shader, 1 = texture (image/video). UI thread.
static int g_visual_source = 0;
static bool g_show_mappings = false;   // P28: the mapping-overview overlay (toggle: M)

static vivid::VisualGraph*      g_vgraph = nullptr;   // source of truth for the generator
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

void draw_clip_preview(vivid::ui::Renderer2D& ui, vivid_poc::Session* s, int t, int sc,
                       const Rect& b, float ar, float ag, float ab, bool on);  // defined below

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
            const bool hov = hit(r, mx, my);
            float ar, ag, ab; track_accent(t, ar, ag, ab);
            const float tbh = 15.f;  // title-bar height
            // cell body (the preview well sits below the title bar)
            ui.draw_rounded_rect(r.x, r.y, r.w, r.h, 4.f, on ? 0.12f : 0.085f, on ? 0.135f : 0.095f, on ? 0.16f : 0.11f, 1.0f);
            // title bar: accent-tinted, brighter when active
            ui.draw_rect(r.x + 1.f, r.y + 1.f, r.w - 2.f, tbh, ar * (on ? 0.55f : 0.28f) + (hov ? 0.05f : 0.f),
                         ag * (on ? 0.55f : 0.28f) + (hov ? 0.05f : 0.f), ab * (on ? 0.55f : 0.28f) + (hov ? 0.05f : 0.f), 1.0f);
            if (q) ui.draw_rect(r.x, r.y, r.w, 2.f, 0.95f, 0.75f, 0.20f, 1.0f);  // queued = gold top edge
            // play glyph (active) then the clip name in the title bar
            float tx = r.x + 6.f;
            if (on) { ui.draw_tri(r.x + 5.f, r.y + 4.f, r.x + 5.f, r.y + 11.f, r.x + 11.f, r.y + 7.5f, 0.6f, 0.95f, 0.6f, 1.0f); tx = r.x + 15.f; }
            char cn[16];
            const int abpm = vivid_poc::session_track_is_audio(s, t) ? vivid_poc::session_audio_clip_bpm(s, t, sc) : 0;
            if (abpm > 0) std::snprintf(cn, sizeof cn, "%d BPM", abpm);
            else          std::snprintf(cn, sizeof cn, "Clip %c", 'A' + sc);
            ui.draw_text(tx, r.y + 3.f, cn, on ? 0.95f : 0.72f, on ? 0.97f : 0.74f, 1.0f, 1.0f, 0.72f);
            // preview fills the body beneath the title bar
            const Rect pv = { r.x + 5.f, r.y + tbh + 4.f, r.w - 10.f, r.h - tbh - 8.f };
            ui.draw_rect(pv.x, pv.y, pv.w, pv.h, 0.03f, 0.035f, 0.045f, 1.0f);
            draw_clip_preview(ui, s, t, sc, pv, ar, ag, ab, on);
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

    // device chain for the selected track: [instrument] [fx... x] [+ FX]
    const int seltr = std::min(std::max(st.sel_track, 0), tracks - 1);
    const float dy = device_y(scenes);
    char dl[64]; std::snprintf(dl, sizeof dl, "DEVICES \xC2\xB7 %s  (click a track header to select)", vivid_poc::session_track_name(s, seltr));
    ui.draw_text(kSceneColX, dy, dl, 0.45f, 0.48f, 0.53f, 1.0f, 0.82f);
    const int nfx = vivid_poc::session_effect_count(s, seltr);
    for (int i = 0; i <= nfx + 1; ++i) {
        const Rect b = device_box(i, scenes);
        const bool hov = hit(b, mx, my);
        const bool isInst = (i == 0), isAdd = (i == nfx + 1);
        const bool aud = vivid_poc::session_track_is_audio(s, seltr);
        if (isInst && aud) continue;  // sampler track has no instrument plugin
        float br = hov ? 0.04f : 0.f;
        ui.draw_rect(b.x, b.y, b.w, b.h, (isAdd ? 0.10f : 0.14f) + br, 0.15f + br, (isInst ? 0.20f : 0.17f) + br, 1.0f);
        ui.draw_rect(b.x, b.y, b.w, 3.f, isInst ? 0.45f : (isAdd ? 0.31f : 0.62f), isInst ? 0.62f : 0.55f, isAdd ? 0.55f : 0.80f, 1.0f);
        if (isAdd) { ui.draw_text(b.x + 10.f, b.y + 13.f, "+ FX", 0.6f, 0.78f, 0.7f, 1.0f, 0.95f); }
        else if (isInst) {
            ui.draw_text(b.x + 8.f, b.y + 6.f, "INST", 0.5f, 0.53f, 0.6f, 1.0f, 0.72f);
            char nm[24]; std::snprintf(nm, sizeof nm, "%.13s", vivid_poc::session_track_name(s, seltr));
            ui.draw_text(b.x + 8.f, b.y + 21.f, nm, 0.88f, 0.9f, 0.94f, 1.0f, 0.9f);
        } else {
            ui.draw_text(b.x + 8.f, b.y + 6.f, "FX", 0.5f, 0.53f, 0.6f, 1.0f, 0.72f);
            char nm[24]; std::snprintf(nm, sizeof nm, "%.12s", vivid_poc::session_effect_name(s, seltr, i - 1));
            ui.draw_text(b.x + 8.f, b.y + 21.f, nm, 0.88f, 0.9f, 0.94f, 1.0f, 0.9f);
            const Rect xb = device_x_btn(i, scenes);
            ui.draw_rect(xb.x, xb.y, xb.w, xb.h, 0.4f, 0.18f, 0.18f, 1.0f);
            ui.draw_text(xb.x + 3.f, xb.y + 1.f, "x", 0.85f, 0.6f, 0.6f, 1.0f, 0.85f);
        }
        if (!isAdd && ((isInst && st.sel_device == 0) || (!isInst && st.sel_device == i))) {
            ui.draw_rect(b.x, b.y + b.h - 3.f, b.w, 3.f, 0.9f, 0.8f, 0.4f, 1.0f);  // selected underline
        }
    }
    // Parameter panel for the selected device.
    const int seldev = std::max(0, st.sel_device);
    const int pc = vivid_poc::session_param_count(s, seltr, seldev);
    const float py0 = params_y(scenes);
    char ph[64];
    const char* devname = (seldev == 0) ? vivid_poc::session_track_name(s, seltr)
                                        : vivid_poc::session_effect_name(s, seltr, seldev - 1);
    std::snprintf(ph, sizeof ph, "PARAMS \xC2\xB7 %.16s  (%d)", devname, pc);
    ui.draw_text(kSceneColX, py0, ph, 0.45f, 0.48f, 0.53f, 1.0f, 0.82f);
    const int shown = std::min(pc, kMaxShownParams);
    for (int i = 0; i < shown; ++i) {
        const Rect r = param_slider_rect(i, scenes);
        const float v = vivid_poc::session_param_value(s, seltr, seldev, i);
        char nm[14]; std::snprintf(nm, sizeof nm, "%.11s", vivid_poc::session_param_name(s, seltr, seldev, i));
        ui.draw_text(kSceneColX, r.y - 2.f, nm, 0.7f, 0.72f, 0.76f, 1.0f, 0.78f);
        // map button (return path): driven if a source is mapped to this param
        const Rect mb = param_map_btn(i, scenes);
        const bool mapped = st.graph && st.graph->source_of(param_dest(seltr, seldev, i)) != nullptr;
        ui.draw_rect(mb.x, mb.y, mb.w, mb.h, mapped ? 0.30f : 0.14f, mapped ? 0.55f : 0.16f, mapped ? 0.55f : 0.2f, 1.0f);
        ui.draw_text(mb.x + 4.f, mb.y - 1.f, "m", mapped ? 0.95f : 0.6f, 0.95f, 0.95f, 1.0f, 0.7f);
        ui.draw_rect(r.x, r.y, r.w, r.h, 0.10f, 0.11f, 0.13f, 1.0f);
        ui.draw_rect(r.x, r.y, r.w * v, r.h, mapped ? 0.45f : 0.35f, mapped ? 0.6f : 0.5f, mapped ? 0.5f : 0.7f, 1.0f);
        ui.draw_rect(r.x + r.w * v - 2.f, r.y - 2.f, 4.f, r.h + 4.f, 0.7f, 0.8f, 0.95f, 1.0f);
    }
    if (pc > shown) ui.draw_text(kSceneColX, py0 + 24.f + shown * 22.f, "...", 0.5f, 0.52f, 0.56f, 1.0f, 0.8f);

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

// The "+ FX" effect picker for the device chain.
void draw_fx_menu(vivid::ui::Renderer2D& ui, const CtxMenu& m) {
    if (!m.open) return;
    const float w = 150.f;
    const int n = vivid_poc::session_available_effect_count();
    ui.draw_rect(m.x, m.y - 22.f, w, 22.f, 0.09f, 0.10f, 0.12f, 1.0f);
    ui.draw_text(m.x + 10.f, m.y - 18.f, "+ effect", 0.55f, 0.58f, 0.64f, 1.0f, 0.82f);
    for (int j = 0; j < n; ++j) {
        const float iy = m.y + j * 24.f;
        ui.draw_rect(m.x, iy, w, 24.f, 0.16f, 0.17f, 0.20f, 1.0f);
        ui.draw_rect(m.x, iy, 3.f, 24.f, 0.31f, 0.70f, 0.80f, 1.0f);
        ui.draw_text(m.x + 12.f, iy + 5.f, vivid_poc::session_available_effect_name(j), 0.85f, 0.88f, 0.92f, 1.0f, 0.9f);
    }
}

// Mini clip preview inside a session cell: a piano-roll for MIDI clips, a
// waveform for audio clips. Drawn faintly so the clip name reads on top.
void draw_clip_preview(vivid::ui::Renderer2D& ui, vivid_poc::Session* s, int t, int sc,
                       const Rect& b, float ar, float ag, float ab, bool on) {
    const float alpha = on ? 0.85f : 0.5f;
    if (vivid_poc::session_track_is_audio(s, t)) {
        float bins[48];
        const int n = vivid_poc::session_audio_waveform(s, t, sc, bins, 48);
        if (n <= 0) return;
        const float midy = b.y + b.h * 0.5f, colw = b.w / n;
        for (int i = 0; i < n; ++i) {
            const float a = std::min(1.f, std::max(0.f, bins[i])) * (b.h * 0.5f - 1.f);
            ui.draw_rect(b.x + colw * i, midy - a, std::max(1.f, colw - 0.4f), a * 2.f + 1.f, ar, ag, ab, alpha);
        }
    } else {
        vivid_poc::ClipNote buf[256];
        const int n = vivid_poc::session_get_clip(s, t, sc, buf, 256);
        const double len = vivid_poc::session_clip_length(s, t, sc);
        if (n <= 0 || len <= 0.0) return;
        int lo = 127, hi = 0;
        for (int i = 0; i < n; ++i) { lo = std::min(lo, buf[i].pitch); hi = std::max(hi, buf[i].pitch); }
        const int span = std::max(12, hi - lo + 1);            // at least an octave
        const int base = lo - (span - (hi - lo + 1)) / 2;      // vertically centered
        for (int i = 0; i < n; ++i) {
            const float x0 = b.x + b.w * static_cast<float>(buf[i].start / len);
            const float ww = b.w * static_cast<float>(buf[i].dur / len);
            const float ny = b.y + b.h * (1.f - (static_cast<float>(buf[i].pitch - base) + 0.5f) / span);
            ui.draw_rect(x0, ny - 1.f, std::max(1.5f, std::min(ww, b.x + b.w - x0)), 2.4f, ar, ag, ab, alpha);
        }
    }
}

// Human-readable label for a mapping destination id.
std::string mapping_dest_label(vivid_poc::Session* s, const std::string& dest) {
    if (dest.rfind("node:", 0) == 0) {  // "node:<id>.<name>" -> "node <id> · <name> (visual)"
        const size_t dot = dest.find('.', 5);
        if (dot != std::string::npos)
            return "node " + dest.substr(5, dot - 5) + " \xC2\xB7 " + dest.substr(dot + 1) + "  (visual)";
    }
    if (dest.rfind("uniform.", 0) == 0) return dest.substr(8) + "  (visual)";
    if (dest.rfind("param:", 0) == 0) {
        int T = -1, D = 0, I = 0;
        if (std::sscanf(dest.c_str(), "param:%d:%d:%d", &T, &D, &I) == 3 && s) {
            const char* pn = vivid_poc::session_param_name(s, T, D, I);
            char buf[72]; std::snprintf(buf, sizeof buf, "T%d %s: %.20s", T, D == 0 ? "inst" : "fx", pn ? pn : "param");
            return buf;
        }
    }
    return dest;
}

// Shared geometry for the mapping overview (so draw + hit-test agree).
struct OvGeom { float px, py, w, h, rowh, hdr; int vis; };
inline OvGeom ov_geom(int n) {
    OvGeom o; o.w = 772.f; o.rowh = 24.f; o.hdr = 58.f;
    o.vis = std::max(1, std::min(n, 15));
    o.h = o.hdr + o.vis * o.rowh + 14.f;
    o.px = (g_win_w - o.w) * 0.5f; o.py = 84.f;
    return o;
}
// Per-row control rects (right-anchored): invert chip, amount/curve/lo/hi +/- steppers, clear.
struct OvRow {
    Rect inv, amtMinus, amtPlus, curMinus, curPlus, loMinus, loPlus, hiMinus, hiPlus, clear;
    float amtValX, curValX, loValX, hiValX;
};
inline OvRow ov_row(float px, float w, float ry) {
    return {
        { px + w - 330.f, ry, 26.f, 18.f },   // inv
        { px + w - 296.f, ry, 13.f, 18.f },   // amt -
        { px + w - 250.f, ry, 13.f, 18.f },   // amt +
        { px + w - 228.f, ry, 13.f, 18.f },   // curve -
        { px + w - 182.f, ry, 13.f, 18.f },   // curve +
        { px + w - 160.f, ry, 13.f, 18.f },   // lo -
        { px + w - 114.f, ry, 13.f, 18.f },   // lo +
        { px + w -  92.f, ry, 13.f, 18.f },   // hi -
        { px + w -  46.f, ry, 13.f, 18.f },   // hi +
        { px + w -  22.f, ry, 14.f, 18.f },   // clear
        px + w - 292.f, px + w - 224.f, px + w - 156.f, px + w - 88.f  // value text x: amt/curve/lo/hi
    };
}

// P28: the mapping overview — every source->dest mapping in one panel, with per-row
// amount/curve steppers + polarity toggle + clear. Direction-colored arrow.
void draw_mapping_overview(vivid::ui::Renderer2D& ui, vivid::ui::NodeGraph* g, vivid_poc::Session* s) {
    if (!g) return;
    const auto& maps = g->mappings();
    const int n = static_cast<int>(maps.size());
    const OvGeom o = ov_geom(n);
    const float px = o.px, py = o.py, w = o.w, rowh = o.rowh, hdr = o.hdr;
    ui.draw_rect(0.f, 40.f, static_cast<float>(g_win_w), static_cast<float>(g_win_h) - 40.f, 0.f, 0.f, 0.f, 0.45f);  // scrim
    ui.draw_rounded_rect(px, py, w, o.h, 6.f, 0.12f, 0.13f, 0.155f, 1.0f);
    ui.draw_rect(px, py, w, 3.f, 0.45f, 0.62f, 0.85f, 1.0f);
    char title[48]; std::snprintf(title, sizeof title, "MAPPINGS  (%d)", n);
    ui.draw_text(px + 16.f, py + 12.f, title, 0.9f, 0.92f, 0.95f, 1.0f, 1.0f);
    ui.draw_text(px + w - 150.f, py + 14.f, "M or Esc to close", 0.5f, 0.52f, 0.56f, 1.0f, 0.78f);
    const OvRow hc = ov_row(px, w, py + 38.f);
    ui.draw_text(px + 16.f, py + 38.f, "SOURCE \xE2\x86\x92 DEST", 0.45f, 0.48f, 0.53f, 1.0f, 0.74f);
    ui.draw_text(hc.inv.x, py + 38.f, "POL", 0.45f, 0.48f, 0.53f, 1.0f, 0.72f);
    ui.draw_text(hc.amtMinus.x + 2.f, py + 38.f, "AMT", 0.45f, 0.48f, 0.53f, 1.0f, 0.72f);
    ui.draw_text(hc.curMinus.x - 2.f, py + 38.f, "CURVE", 0.45f, 0.48f, 0.53f, 1.0f, 0.72f);
    ui.draw_text(hc.loMinus.x + 4.f, py + 38.f, "LO", 0.45f, 0.48f, 0.53f, 1.0f, 0.72f);
    ui.draw_text(hc.hiMinus.x + 4.f, py + 38.f, "HI", 0.45f, 0.48f, 0.53f, 1.0f, 0.72f);
    if (n == 0) {
        ui.draw_text(px + 16.f, py + hdr + 6.f, "No mappings yet \xE2\x80\x94 wire a data node to an op param, or map a device param (m).",
                     0.55f, 0.57f, 0.6f, 1.0f, 0.84f);
        return;
    }
    auto stepper = [&](const Rect& minus, const Rect& plus, float valX, float ry, const char* val) {
        ui.draw_rect(minus.x, minus.y, minus.w, minus.h, 0.18f, 0.19f, 0.22f, 1.0f);
        ui.draw_text(minus.x + 4.f, ry + 3.f, "-", 0.8f, 0.82f, 0.86f, 1.0f, 0.9f);
        ui.draw_rect(plus.x, plus.y, plus.w, plus.h, 0.18f, 0.19f, 0.22f, 1.0f);
        ui.draw_text(plus.x + 3.f, ry + 3.f, "+", 0.8f, 0.82f, 0.86f, 1.0f, 0.9f);
        ui.draw_text(valX, ry + 4.f, val, 0.78f, 0.81f, 0.85f, 1.0f, 0.82f);
    };
    for (int i = 0; i < o.vis; ++i) {
        const auto& m = maps[i];
        const float ry = py + hdr + i * rowh;
        if (i & 1) ui.draw_rect(px + 2.f, ry - 2.f, w - 4.f, rowh, 0.14f, 0.15f, 0.18f, 0.5f);
        const bool toVisual = m.dest.rfind("node:", 0) == 0 || m.dest.rfind("uniform.", 0) == 0;
        const bool fromViz = m.source.rfind("viz.", 0) == 0;
        float cr = 0.5f, cg = 0.7f, cb = 0.85f;                       // audio->audio
        if (toVisual) { cr = 0.31f; cg = 0.80f; cb = 0.75f; }         // audio->visual
        else if (fromViz) { cr = 0.85f; cg = 0.7f; cb = 0.4f; }       // visual->audio
        const OvRow rc = ov_row(px, w, ry);
        char src8[20]; std::snprintf(src8, sizeof src8, "%.18s", m.source.c_str());
        ui.draw_text(px + 16.f, ry + 4.f, src8, 0.85f, 0.88f, 0.92f, 1.0f, 0.82f);
        ui.draw_text(px + 150.f, ry + 3.f, "\xE2\x86\x92", cr, cg, cb, 1.0f, 0.92f);
        char dst22[26]; std::snprintf(dst22, sizeof dst22, "%.24s", mapping_dest_label(s, m.dest).c_str());
        ui.draw_text(px + 168.f, ry + 4.f, dst22, 0.82f, 0.85f, 0.9f, 1.0f, 0.82f);
        // polarity chip
        ui.draw_rect(rc.inv.x, rc.inv.y, rc.inv.w, rc.inv.h, m.invert ? 0.5f : 0.18f, m.invert ? 0.4f : 0.19f, m.invert ? 0.55f : 0.22f, 1.0f);
        ui.draw_text(rc.inv.x + 5.f, ry + 3.f, "inv", m.invert ? 0.95f : 0.6f, 0.9f, 0.95f, 1.0f, 0.74f);
        char amt[10]; std::snprintf(amt, sizeof amt, "%.2f", m.amount);
        stepper(rc.amtMinus, rc.amtPlus, rc.amtValX, ry, amt);
        char cur[10]; std::snprintf(cur, sizeof cur, "%+.2f", m.curve);
        stepper(rc.curMinus, rc.curPlus, rc.curValX, ry, cur);
        char lo[10]; std::snprintf(lo, sizeof lo, "%.2f", m.out_lo);
        stepper(rc.loMinus, rc.loPlus, rc.loValX, ry, lo);
        char hi[10]; std::snprintf(hi, sizeof hi, "%.2f", m.out_hi);
        stepper(rc.hiMinus, rc.hiPlus, rc.hiValX, ry, hi);
        ui.draw_text(rc.clear.x + 2.f, ry + 3.f, "\xC3\x97", 0.75f, 0.45f, 0.45f, 1.0f, 0.95f);
    }
    if (n > o.vis) {
        char more[24]; std::snprintf(more, sizeof more, "+%d more\xE2\x80\xA6", n - o.vis);
        ui.draw_text(px + 16.f, py + hdr + o.vis * rowh + 2.f, more, 0.5f, 0.52f, 0.56f, 1.0f, 0.8f);
    }
}

// The "map this param from a source" picker (the return path).
void draw_map_menu(vivid::ui::Renderer2D& ui, const CtxMenu& m) {
    if (!m.open) return;
    const float w = 168.f;
    ui.draw_rect(m.x, m.y - 22.f, w, 22.f, 0.09f, 0.10f, 0.12f, 1.0f);
    ui.draw_text(m.x + 10.f, m.y - 18.f, "map param from:", 0.55f, 0.58f, 0.64f, 1.0f, 0.8f);
    for (int j = 0; j < kNumMapSources; ++j) {
        const float iy = m.y + j * 24.f;
        ui.draw_rect(m.x, iy, w, 24.f, 0.16f, 0.17f, 0.20f, 1.0f);
        ui.draw_rect(m.x, iy, 3.f, 24.f, 0.85f, 0.7f, 0.4f, 1.0f);  // amber = return path
        ui.draw_text(m.x + 12.f, iy + 5.f, kMapSources[j].label, 0.85f, 0.88f, 0.92f, 1.0f, 0.88f);
    }
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
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (!st) return;
    // The operator chooser captures the keyboard while open (repeat allowed for nav).
    if (st->graph && st->graph->chooser_open()) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            if (key == GLFW_KEY_ESCAPE) st->graph->chooser_hide();
            else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) st->graph->chooser_confirm();
            else if (key == GLFW_KEY_DOWN || key == GLFW_KEY_TAB) st->graph->chooser_move(+1);
            else if (key == GLFW_KEY_UP) st->graph->chooser_move(-1);
            else if (key == GLFW_KEY_BACKSPACE) st->graph->chooser_backspace();
        }
        return;  // swallow all keys while the chooser is up
    }
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE && st->editor && st->editor->is_open()) { st->editor->close(); return; }
    if (key == GLFW_KEY_ESCAPE && g_show_mappings) { g_show_mappings = false; return; }
    if (key == GLFW_KEY_M) { g_show_mappings = !g_show_mappings; return; }  // mapping overview
    // Tab -> open the operator chooser at the cursor (visuals pane only).
    if (key == GLFW_KEY_TAB && st->graph) {
        double mx, my; glfwGetCursorPos(w, &mx, &my);
        if (mx >= g_split_x) { st->graph->chooser_show(mx, my); return; }
    }

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
    if (key == GLFW_KEY_V && g_vgraph) {  // toggle the visuals generator (also via the generator node)
        g_vgraph->set_generator(g_vgraph->generator() == vivid::VOp::Video ? vivid::VOp::Plasma : vivid::VOp::Video);
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

void char_callback(GLFWwindow* w, unsigned int cp) {
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (st && st->graph && st->graph->chooser_open()) st->graph->chooser_char(cp);
}

void scroll_callback(GLFWwindow* w, double /*xoff*/, double yoff) {
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (!st) return;
    double mx, my; glfwGetCursorPos(w, &mx, &my);
    if (st->editor && st->editor->is_open() && st->editor->contains(mx, my)) { st->editor->scroll(yoff); return; }
    // Scroll over the visuals pane zooms the node graph around the cursor.
    if (st->graph && mx >= g_split_x) st->graph->zoom_at(mx, my, std::pow(1.12f, static_cast<float>(yoff)));
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

    // Mapping overview is modal while open: per-row steppers/toggle/clear; click-away closes.
    if (g_show_mappings && st->graph && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        const auto& maps = st->graph->mappings();
        const OvGeom o = ov_geom(static_cast<int>(maps.size()));
        if (mx >= o.px && mx < o.px + o.w && my >= o.py && my < o.py + o.h) {
            for (int i = 0; i < o.vis; ++i) {
                const float ry = o.py + o.hdr + i * o.rowh;
                if (my < ry || my >= ry + o.rowh) continue;
                const OvRow rc = ov_row(o.px, o.w, ry);
                const std::string& d = maps[i].dest;
                if (hit(rc.inv, mx, my))      { st->graph->toggle_mapping_invert(d); return; }
                if (hit(rc.amtMinus, mx, my)) { st->graph->set_mapping_amount(d, std::max(0.f, maps[i].amount - 0.1f)); return; }
                if (hit(rc.amtPlus, mx, my))  { st->graph->set_mapping_amount(d, std::min(4.f, maps[i].amount + 0.1f)); return; }
                if (hit(rc.curMinus, mx, my)) { st->graph->set_mapping_curve(d, std::max(-1.f, maps[i].curve - 0.25f)); return; }
                if (hit(rc.curPlus, mx, my))  { st->graph->set_mapping_curve(d, std::min(1.f, maps[i].curve + 0.25f)); return; }
                if (hit(rc.loMinus, mx, my))  { st->graph->set_mapping_lo(d, std::max(0.f, maps[i].out_lo - 0.1f)); return; }
                if (hit(rc.loPlus, mx, my))   { st->graph->set_mapping_lo(d, std::min(1.f, maps[i].out_lo + 0.1f)); return; }
                if (hit(rc.hiMinus, mx, my))  { st->graph->set_mapping_hi(d, std::max(0.f, maps[i].out_hi - 0.1f)); return; }
                if (hit(rc.hiPlus, mx, my))   { st->graph->set_mapping_hi(d, std::min(1.f, maps[i].out_hi + 0.1f)); return; }
                if (hit(rc.clear, mx, my))    { st->graph->disconnect_dest(d); return; }
                break;
            }
            return;  // click inside the panel: consume
        }
        g_show_mappings = false; return;  // click outside: close
    }

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
        const double now = glfwGetTime();
        if (now - st->split_last_t < 0.35) { g_split_x = std::round(g_win_w * 0.46f); st->split_drag = false; st->split_last_t = -1.0; }
        else { st->split_drag = true; st->split_last_t = now; }
        return;
    }

    // Right-click a meter (master or per-track) -> open its characteristic menu.
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        const int src = st->session ? meter_hit(tracks, scenes, mx, my) : -2;
        if (src != -2) st->menu = { true, static_cast<float>(mx), static_cast<float>(my), src };
        else st->menu.open = false;
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    if (action == GLFW_RELEASE) { st->gain_drag = -1; st->param_drag = -1; if (st->graph) st->graph->on_up(mx, my); return; }
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
    // FX menu has priority: pick an effect -> add it to the menu's track.
    if (st->fx_menu.open) {
        for (int j = 0; j < vivid_poc::session_available_effect_count(); ++j) {
            const Rect r = { st->fx_menu.x, st->fx_menu.y + j * 24.f, 150.f, 24.f };
            if (hit(r, mx, my)) { vivid_poc::session_add_effect_by_index(st->session, st->fx_menu.src, j); break; }
        }
        st->fx_menu.open = false;
        return;
    }
    // Map menu: pick a source to drive the selected param (the return path).
    if (st->map_menu.open) {
        const int seltr = std::min(std::max(st->sel_track, 0), tracks - 1);
        const int seldev = std::max(0, st->sel_device);
        for (int j = 0; j < kNumMapSources; ++j) {
            const Rect rr = { st->map_menu.x, st->map_menu.y + j * 24.f, 168.f, 24.f };
            if (hit(rr, mx, my) && st->graph) {
                const std::string d = param_dest(seltr, seldev, st->map_param);
                if (kMapSources[j].id[0] == '\0') st->graph->disconnect_dest(d);
                else st->graph->add_mapping(kMapSources[j].id, d, 1.0f);
                break;
            }
        }
        st->map_menu.open = false;
        return;
    }
    if (!st->session) return;

    // A meter (master or per-track) -> open its characteristic menu (left-click).
    {
        const int src = meter_hit(tracks, scenes, mx, my);
        if (src != -2) { st->menu = { true, static_cast<float>(mx), static_cast<float>(my), src }; return; }
    }
    // Click a track header -> select it (its device chain shows in the DAW pane).
    for (int t = 0; t < tracks; ++t)
        if (hit(track_header_rect(t), mx, my)) { st->sel_track = t; return; }

    // Device chain of the selected track: single-click selects (shows params),
    // double-click opens the plugin editor; x removes an effect; + FX adds one.
    {
        const int seltr = std::min(std::max(st->sel_track, 0), tracks - 1);
        const int nfx = vivid_poc::session_effect_count(st->session, seltr);
        const double now = glfwGetTime();
        auto open_dev = [&](int dev) {
            auto* ctrl = static_cast<Steinberg::Vst::IEditController*>(
                dev == 0 ? vivid_poc::session_track_controller(st->session, seltr)
                         : vivid_poc::session_effect_controller(st->session, seltr, dev - 1));
            if (!ctrl) return;
            const char* nm = dev == 0 ? vivid_poc::session_track_name(st->session, seltr)
                                      : vivid_poc::session_effect_name(st->session, seltr, dev - 1);
            if (dev == 0) {
                if (st->track_win[seltr]) { vst3_plugin_window_close(st->track_win[seltr]); st->track_win[seltr] = nullptr; }
                st->track_win[seltr] = vst3_plugin_window_open(ctrl, nm);
            } else {
                int slot = -1; for (int k = 0; k < 8; ++k) if (!st->fx_win[k]) { slot = k; break; }
                if (slot >= 0) st->fx_win[slot] = vst3_plugin_window_open(ctrl, nm);
            }
        };
        auto click_dev = [&](int dev) {
            if (st->last_dev_i == dev && now - st->last_dev_t < 0.35) { open_dev(dev); st->last_dev_t = -1; }
            else { st->sel_device = dev; st->last_dev_i = dev; st->last_dev_t = now; }
        };
        if (!vivid_poc::session_track_is_audio(st->session, seltr) && hit(device_box(0, scenes), mx, my)) { click_dev(0); return; }
        for (int e = 0; e < nfx; ++e) {
            if (hit(device_x_btn(1 + e, scenes), mx, my)) {
                vivid_poc::session_remove_effect(st->session, seltr, e);
                if (st->sel_device > nfx - 1) st->sel_device = 0;
                return;
            }
            if (hit(device_box(1 + e, scenes), mx, my)) { click_dev(1 + e); return; }
        }
        if (hit(device_box(1 + nfx, scenes), mx, my)) {
            st->fx_menu = { true, static_cast<float>(mx), static_cast<float>(my), seltr };
            return;
        }
        // parameter sliders for the selected device
        const int seldev = std::max(0, st->sel_device);
        const int npc = std::min(vivid_poc::session_param_count(st->session, seltr, seldev), kMaxShownParams);
        for (int i = 0; i < npc; ++i) {
            if (hit(param_map_btn(i, scenes), mx, my)) {  // open the source picker for this param
                st->map_menu = { true, static_cast<float>(mx), static_cast<float>(my), 0 };
                st->map_param = i; return;
            }
            const Rect r = param_slider_rect(i, scenes);
            if (hit(r, mx, my)) {
                st->param_drag = i;
                const float v = std::min(1.0, std::max(0.0, (mx - r.x) / r.w));
                vivid_poc::session_set_param(st->session, seltr, seldev,
                                             vivid_poc::session_param_id(st->session, seltr, seldev, i), v);
                return;
            }
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

    // Composable visuals chain (generator -> feedback -> blur -> viewer).
    const uint32_t kRtW = static_cast<uint32_t>(kViewW), kRtH = static_cast<uint32_t>(kViewH);
    vivid::VisualGraph vgraph;
    if (!vgraph.init(gpu.device(), gpu.queue(), gpu.surface_format(), kRtW, kRtH))
        std::fprintf(stderr, "[vivid] visual graph init failed (viewer disabled)\n");
    g_vgraph = &vgraph;

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
    graph.set_visual_graph(&vgraph);   // show the op-chain; generator node toggles Plasma/Video
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
    glfwSetCharCallback(window, char_callback);
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
          g_split_x = std::clamp(g_split_x, 40.f, static_cast<float>(g_win_w) - 40.f);
          clip_editor.set_window(static_cast<float>(g_win_w), static_cast<float>(g_win_h));
        }

        // reap plugin editor windows the user closed (instruments + effects)
        for (int t = 0; audio_state.session && t < vivid_poc::session_track_count(audio_state.session); ++t)
            if (audio_state.track_win[t] && !vst3_plugin_window_is_open(audio_state.track_win[t])) {
                vst3_plugin_window_close(audio_state.track_win[t]); audio_state.track_win[t] = nullptr;
            }
        for (int k = 0; k < 8; ++k)
            if (audio_state.fx_win[k] && !vst3_plugin_window_is_open(audio_state.fx_win[k])) {
                vst3_plugin_window_close(audio_state.fx_win[k]); audio_state.fx_win[k] = nullptr;
            }

        const double beats = transport.beats.load(std::memory_order_relaxed);
        const float level = transport.level.load(std::memory_order_relaxed);
        react += (std::min(1.0f, level * 5.0f) - react) * 0.3f;            // smoothed level
        trHold *= 0.85f;                                                   // decay the held peak
        trHold = std::max(trHold, transport.transient.load(std::memory_order_relaxed));
        graph.set_value(0, react);                 // master level
        graph.set_value(1, std::min(1.0f, trHold)); // master transient
        graph.set_value(2, std::min(1.0f, transport.band_low.load(std::memory_order_relaxed) * 5.0f));
        graph.set_value(3, std::min(1.0f, transport.band_mid.load(std::memory_order_relaxed) * 8.0f));
        graph.set_value(4, std::min(1.0f, transport.band_high.load(std::memory_order_relaxed) * 12.0f));
        for (int t = 0; audio_state.session && t < vivid_poc::session_track_count(audio_state.session) && t < 8; ++t) {
            const float lv = vivid_poc::session_track_level(audio_state.session, t);
            trkReact[t] += (std::min(1.0f, lv * 5.0f) - trkReact[t]) * 0.3f;
            trkTrHold[t] *= 0.85f;
            trkTrHold[t] = std::max(trkTrHold[t], vivid_poc::session_track_transient(audio_state.session, t));
            graph.set_value(char_id_for(t, 0), trkReact[t]);
            graph.set_value(char_id_for(t, 1), std::min(1.0f, trkTrHold[t]));
            graph.set_value(char_id_for(t, 2), std::min(1.0f, vivid_poc::session_track_band(audio_state.session, t, 0) * 5.0f));
            graph.set_value(char_id_for(t, 3), std::min(1.0f, vivid_poc::session_track_band(audio_state.session, t, 1) * 8.0f));
            graph.set_value(char_id_for(t, 4), std::min(1.0f, vivid_poc::session_track_band(audio_state.session, t, 2) * 12.0f));
        }
        // Resolve each visual node's params from the registry (writes into the
        // VisualGraph nodes) and publish the viz.* return-path sources.
        graph.apply_params();
        // Apply any source -> audio-param mappings ("param:T:D:I").
        if (audio_state.session)
            for (const auto& m : graph.mappings()) {
                if (m.dest.rfind("param:", 0) != 0) continue;
                int T = -1, D = 0, I = 0;
                if (std::sscanf(m.dest.c_str(), "param:%d:%d:%d", &T, &D, &I) == 3 && T >= 0)
                    vivid_poc::session_set_param(audio_state.session, T, D,
                                                 vivid_poc::session_param_id(audio_state.session, T, D, I),
                                                 graph.dest_value(m.dest));
            }

        double mx, my; glfwGetCursorPos(window, &mx, &my);
        if (audio_state.split_drag)  // continue a splitter drag (either pane can collapse)
            g_split_x = std::clamp(static_cast<float>(mx), 40.f, static_cast<float>(g_win_w) - 40.f);
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
        if (audio_state.param_drag >= 0 && audio_state.session) {  // continue a param slider drag
            const int scenes = vivid_poc::session_scene_count(audio_state.session);
            const int ntr = vivid_poc::session_track_count(audio_state.session);
            const int seltr = std::min(std::max(audio_state.sel_track, 0), ntr - 1);
            const int seldev = std::max(0, audio_state.sel_device);
            const Rect r = param_slider_rect(audio_state.param_drag, scenes);
            const float v = std::min(1.0, std::max(0.0, (mx - r.x) / r.w));
            vivid_poc::session_set_param(audio_state.session, seltr, seldev,
                                         vivid_poc::session_param_id(audio_state.session, seltr, seldev, audio_state.param_drag), v);
        }

        vivid::FrameState frame;
        if (gpu.begin_frame(frame)) {
            const float tsec = static_cast<float>(glfwGetTime());
            // the generator (set by V or the generator node) drives the video source
            g_visual_source = (vgraph.generator() == vivid::VOp::Video) ? 1 : 0;
            if (g_video) video_play(g_video, g_visual_source == 1);
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
            // composable visuals chain -> viewer (per-node params, set by apply_params)
            clear_pass(frame.encoder, frame.view, 0.045f, 0.05f, 0.06f);  // static dark backdrop
            const Rect vp = viewer_rect();
            vgraph.render(frame.encoder, frame.view, vp.x, vp.y, vp.w, vp.h, tsec, srcTex.view);

            draw_ui(ui, audio_state, beats, mx, my);
            const Rect vrp = viewer_rect();
            graph.set_bounds(g_split_x + 8.f, vrp.y + vrp.h + 16.f,
                             static_cast<float>(g_win_w) - 8.f, static_cast<float>(g_win_h) - 8.f);
            graph.draw(ui);   // includes live node thumbnails via draw_texture
            // Pass 1: DAW + node graph (cards + thumbnails composite in-batch).
            ui.flush(frame.encoder, frame.view, g_win_w, g_win_h, g_win_w, g_win_h);
            // Pass 2: floating overlays — drawn AFTER pass 1 so they sit on top.
            graph.draw_overlays(ui);  // operator chooser
            draw_menu(ui, audio_state.menu,
                      audio_state.menu.src < 0 ? "Master"
                      : (audio_state.session ? vivid_poc::session_track_name(audio_state.session, audio_state.menu.src) : "track"));
            draw_fx_menu(ui, audio_state.fx_menu);
            draw_map_menu(ui, audio_state.map_menu);
            clip_editor.draw(ui);  // editor window on top
            if (g_show_mappings) draw_mapping_overview(ui, audio_state.graph, audio_state.session);
            ui.flush(frame.encoder, frame.view, g_win_w, g_win_h, g_win_w, g_win_h);
            gpu.end_frame(frame);
        }
        return true;
    };
    vivid::macos_run_frame_loop(poll_events, tick);

    if (audio_ok) ma_device_uninit(&device);  // stops the callback first
    for (int t = 0; t < 8; ++t) if (audio_state.track_win[t]) vst3_plugin_window_close(audio_state.track_win[t]);
    for (int k = 0; k < 8; ++k) if (audio_state.fx_win[k]) vst3_plugin_window_close(audio_state.fx_win[k]);
    if (audio_state.session) vivid_poc::session_destroy(audio_state.session);
    if (g_video) { video_close(g_video); g_video = nullptr; }
    vgraph.shutdown();
    srcTex.release();
    ui.shutdown();
    gpu.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
