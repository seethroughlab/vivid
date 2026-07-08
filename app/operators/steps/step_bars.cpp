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
    static constexpr const char* kDisplayName = "Step Bars";
    static constexpr const char* kSummary = "Example op with a custom editor: 8 draggable step bars.";
    static constexpr std::array<const char*, 3> kKeywords = {"editor", "steps", "example"};

    vivid::Param<float> s[kSteps] = {
        {"s0", 0.30f, 0.f, 1.f}, {"s1", 0.55f, 0.f, 1.f}, {"s2", 0.80f, 0.f, 1.f}, {"s3", 0.45f, 0.f, 1.f},
        {"s4", 0.65f, 0.f, 1.f}, {"s5", 0.25f, 0.f, 1.f}, {"s6", 0.90f, 0.f, 1.f}, {"s7", 0.50f, 0.f, 1.f},
    };

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

// Draw the 8 step values as draggable bars. Reads live values from ctx->param_values and writes
// edits via ctx->commands.set_param (host-authoritative). `instance` is unused — the host node's
// base params are the source of truth, not the op instance.
extern "C" void vivid_draw_editor(void* /*instance*/, VividEditorContext* ctx) {
    if (!ctx) return;
    const VividDrawAPI& d = ctx->draw;
    const VividInspectorTheme& th = ctx->theme;
    const float W = ctx->surface_width, H = ctx->surface_height;
    const float pad = 14.f, top = 30.f, botpad = 22.f;
    const float plot_h = H - top - botpad;
    const float col_w = (W - 2 * pad) / float(kSteps);

    d.draw_rect(d.opaque, 0, 0, W, H, th.dark_bg);
    d.draw_text(d.opaque, pad, 8, "STEP BARS — drag a bar to set its value", th.dim_text, 0.85f);

    const int n = ctx->param_count < (uint32_t)kSteps ? (int)ctx->param_count : kSteps;
    // Which column is the pointer over (for hover + drag).
    const float mx = ctx->mouse.x, my = ctx->mouse.y;
    int hover = -1;
    if (mx >= pad && mx < W - pad && my >= top && my <= top + plot_h)
        hover = (int)((mx - pad) / col_w);
    if (hover >= n) hover = -1;

    for (int i = 0; i < n; ++i) {
        const float v = ctx->param_values ? ctx->param_values[i] : 0.f;
        const float x = pad + i * col_w + 2.f;
        const float w = col_w - 4.f;
        const float bh = v * plot_h;
        const float y = top + (plot_h - bh);
        d.draw_rect(d.opaque, x, top, w, plot_h, th.slider_track);        // track
        VividColor fill = (i == hover) ? th.accent : th.slider_fill;
        d.draw_rounded_rect(d.opaque, x, y, w, bh, th.corner_radius, fill);
        char buf[16]; std::snprintf(buf, sizeof buf, "%.2f", v);
        d.draw_text(d.opaque, x + 2.f, top + plot_h + 4.f, buf, th.dim_text, 0.7f);
    }
    d.draw_line(d.opaque, pad, top + plot_h, W - pad, top + plot_h, 1.f, th.separator);

    // Drag: while the button is held over a column, set that step to the pointer height.
    if (ctx->mouse.left_down && hover >= 0 && ctx->commands.set_param) {
        float v = 1.f - (my - top) / plot_h;
        v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        char name[4] = { 's', (char)('0' + hover), 0, 0 };
        ctx->commands.set_param(ctx->commands.opaque, name, v);
    }
    ctx->wants_keyboard = 0;
}
