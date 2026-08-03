// UI-4b example: a loadable operator that exports a CUSTOM EDITOR (vivid_editor_metadata +
// vivid_draw_editor) — the first op to exercise the editor ABI end to end. It has 8 step params
// (s0..s7) that would be tedious as 8 knobs; the editor draws them as draggable bars instead and
// writes edits back through ctx->commands.set_param (host-authoritative — the editor never mutates
// the instance directly). process_gpu renders the 8 values as a bar chart so edits are visible in
// the node's output too.
//
// Self-contained against operator_api/ only (like spike_solid). VIVID_REGISTER emits the standard
// create/destroy/descriptor/process surface; the two editor entry points are exported by hand.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
constexpr int kSteps = 8;
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
const char* kBarsWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U { a: vec4f, b: vec4f };   // 8 step values packed into two vec4
@group(0) @binding(0) var<uniform> u: U;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let bar = u32(clamp(inp.uv.x, 0.0, 0.999) * 8.0);
    var v = 0.0;
    if (bar < 4u) { v = u.a[bar]; } else { v = u.b[bar - 4u]; }
    let h = 1.0 - inp.uv.y;                 // 0 at bottom, 1 at top
    let on = select(0.12, 1.0, h <= v);     // fill up to the value
    let col = 0.35 + 0.55 * f32(bar) / 7.0; // per-bar tint so columns read apart
    return vec4f(col * on, 0.4 * on, (1.0 - col) * on + 0.08, 1.0);
}
)";
}  // namespace

struct StepBarsOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "StepBars";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;   // ADR-0046: self-contained texture generator
    static constexpr const char* kDisplayName = "Step Bars";
    static constexpr const char* kSummary = "Example op with a custom editor: 8 draggable step bars.";
    static constexpr std::array<const char*, 3> kKeywords = {"editor", "steps", "example"};

    vivid::Param<float> s[kSteps] = {
        {"s0", 0.30f, 0.f, 1.f}, {"s1", 0.55f, 0.f, 1.f}, {"s2", 0.80f, 0.f, 1.f}, {"s3", 0.45f, 0.f, 1.f},
        {"s4", 0.65f, 0.f, 1.f}, {"s5", 0.25f, 0.f, 1.f}, {"s6", 0.90f, 0.f, 1.f}, {"s7", 0.50f, 0.f, 1.f},
    };

    int editor_sel_ = 0;   // keyboard-selected column (editor-only UI state, lives on the instance)

    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;

    ~StepBarsOp() override {
        if (bg_) wgpuBindGroupRelease(bg_);
        if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_);
        if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_);
        if (sh_) wgpuShaderModuleRelease(sh_);
    }

    void collect_params(std::vector<vivid::ParamBase*>& o) override { for (auto& p : s) o.push_back(&p); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }

    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kBarsWGSL, "StepBars", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "StepBars U");
        WGPUBindGroupLayoutEntry e{};
        e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 32;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "StepBars Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 32;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }

    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        float u[8];
        for (int i = 0; i < kSteps; ++i) u[i] = c->param_values ? c->param_values[i] : s[i].value;
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "StepBars");
    }
};

VIVID_REGISTER(StepBarsOp)

// --- Custom editor ABI (hand-exported; VIVID_REGISTER does not emit these) -------------------
extern "C" VividEditorMetadata vivid_editor_metadata(void) {
    VividEditorMetadata m{};
    m.default_width = 420; m.default_height = 200;
    m.min_width = 260; m.min_height = 140;
    m.title_suffix = " — Steps";
    return m;
}

// GLFW key codes + modifier bits (the editor ABI passes them through verbatim — see VividEditorEvent
// `key`/`modifiers`). Hardcoded so the op stays self-contained against operator_api only (no GLFW).
namespace {
constexpr int kKeyRight = 262, kKeyLeft = 263, kKeyDown = 264, kKeyUp = 265, kKeyC = 67, kKeyV = 86;
constexpr int kModShift = 0x0001, kModCtrl = 0x0002, kModSuper = 0x0008;
}

// The 8 step values as bars. UI-5.4 exercises the full editor input surface: drag (mouse), scroll
// over a column to nudge it, arrow keys to select+adjust the keyboard column, ⌘/Ctrl-C/V to
// copy/paste the whole pattern via the host clipboard, a crosshair cursor over the plot, and a
// status footer. Editor-only UI state (the keyboard selection) lives on the op instance.
extern "C" void vivid_draw_editor(void* instance, VividEditorContext* ctx) {
    if (!ctx) return;
    auto* self = static_cast<StepBarsOp*>(instance);
    const VividDrawAPI& d = ctx->draw;
    const VividInspectorTheme& th = ctx->theme;
    const float W = ctx->surface_width, H = ctx->surface_height;
    const float pad = 14.f, top = 30.f, botpad = 22.f;
    const float plot_h = H - top - botpad;
    const float col_w = (W - 2 * pad) / float(kSteps);
    const int n = ctx->param_count < (uint32_t)kSteps ? (int)ctx->param_count : kSteps;

    auto get_step = [&](int i) -> float { return (ctx->param_values && i >= 0 && i < n) ? ctx->param_values[i] : 0.f; };
    auto set_step = [&](int i, float v) {
        if (i < 0 || i >= n || !ctx->commands.set_param) return;
        v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        char nm[4] = { 's', (char)('0' + i), 0, 0 };
        ctx->commands.set_param(ctx->commands.opaque, nm, v);
    };
    auto col_at = [&](float x, float y) -> int {
        if (x < pad || x >= W - pad || y < top || y > top + plot_h) return -1;
        const int c = (int)((x - pad) / col_w); return c < n ? c : -1;
    };

    static int sel_fallback = 0;
    int& sel = self ? self->editor_sel_ : sel_fallback;   // keyboard selection (instance state)
    if (sel >= n) sel = n - 1; if (sel < 0) sel = 0;

    // --- process the event stream (scroll / keys / clipboard) ---
    for (uint32_t k = 0; k < ctx->event_count; ++k) {
        const VividEditorEvent& e = ctx->events[k];
        if (e.type == VIVID_EDITOR_EVENT_MOUSE_SCROLL) {
            const int c = col_at(e.x, e.y);
            const int target = c >= 0 ? c : sel;
            set_step(target, get_step(target) + e.scroll_dy * 0.05f);
        } else if (e.type == VIVID_EDITOR_EVENT_KEY && e.action >= 1) {   // press or repeat
            const bool cmd = (e.modifiers & (kModCtrl | kModSuper)) != 0;
            if (e.key == kKeyLeft)       sel = sel > 0 ? sel - 1 : 0;
            else if (e.key == kKeyRight) sel = sel < n - 1 ? sel + 1 : n - 1;
            else if (e.key == kKeyUp)    set_step(sel, get_step(sel) + ((e.modifiers & kModShift) ? 0.01f : 0.05f));
            else if (e.key == kKeyDown)  set_step(sel, get_step(sel) - ((e.modifiers & kModShift) ? 0.01f : 0.05f));
            else if (e.key == kKeyC && cmd && ctx->host.set_clipboard_text) {   // copy the pattern
                char buf[96]; int o = 0;
                for (int i = 0; i < n; ++i) o += std::snprintf(buf + o, sizeof buf - o, i ? ",%.3f" : "%.3f", get_step(i));
                ctx->host.set_clipboard_text(ctx->host.opaque, buf);
                if (ctx->host.set_status_text) ctx->host.set_status_text(ctx->host.opaque, "copied pattern to clipboard");
            } else if (e.key == kKeyV && cmd && ctx->host.get_clipboard_text) {   // paste the pattern
                const char* txt = ctx->host.get_clipboard_text(ctx->host.opaque);
                if (txt) { const char* p = txt;
                    for (int i = 0; i < n && *p; ++i) { char* end = nullptr; float v = std::strtof(p, &end);
                        if (end == p) break; set_step(i, v); p = (*end == ',') ? end + 1 : end; }
                    if (ctx->host.set_status_text) ctx->host.set_status_text(ctx->host.opaque, "pasted pattern from clipboard");
                }
            }
        }
    }

    // Pointer column (for hover + drag) + crosshair cursor over the plot.
    const float mx = ctx->mouse.x, my = ctx->mouse.y;
    const int hover = col_at(mx, my);
    if (hover >= 0 && ctx->host.set_cursor) ctx->host.set_cursor(ctx->host.opaque, VIVID_CURSOR_CROSSHAIR);
    ctx->wants_keyboard = 1;   // this editor wants key events

    // --- draw ---
    d.draw_rect(d.opaque, 0, 0, W, H, th.dark_bg);
    d.draw_text(d.opaque, pad, 8, "STEP BARS — drag / scroll / \xE2\x86\x90\xE2\x86\x92 select \xC2\xB7 \xE2\x86\x91\xE2\x86\x93 adjust \xC2\xB7 Cmd-C/V", th.dim_text, 0.78f);
    for (int i = 0; i < n; ++i) {
        const float v = get_step(i);
        const float x = pad + i * col_w + 2.f, w = col_w - 4.f;
        const float bh = v * plot_h, y = top + (plot_h - bh);
        d.draw_rect(d.opaque, x, top, w, plot_h, th.slider_track);
        VividColor fill = (i == hover) ? th.accent : th.slider_fill;
        d.draw_rounded_rect(d.opaque, x, y, w, bh, th.corner_radius, fill);
        if (i == sel) d.draw_line(d.opaque, x, top - 3.f, x + w, top - 3.f, 2.f, th.accent);   // selection marker
        char buf[16]; std::snprintf(buf, sizeof buf, "%.2f", v);
        d.draw_text(d.opaque, x + 2.f, top + plot_h + 4.f, buf, (i == sel) ? th.bright_text : th.dim_text, 0.7f);
    }
    d.draw_line(d.opaque, pad, top + plot_h, W - pad, top + plot_h, 1.f, th.separator);

    // Drag: while the button is held over a column, set that step to the pointer height.
    if (ctx->mouse.left_down && hover >= 0) {
        set_step(hover, 1.f - (my - top) / plot_h);
        sel = hover;   // dragging also moves the keyboard selection
    }
}
