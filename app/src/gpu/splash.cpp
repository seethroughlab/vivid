#include "gpu/splash.h"
#include "gpu/gpu_context.h"
#include "gpu/gpu_util.h"        // to_sv + kMsaaSamples
#include "ui/renderer_2d.h"
#include "version.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <random>

namespace vivid {

// Nebula background + node-graph "V" logo. Ported verbatim from vivid-classic's splash
// fragment (main.cpp); the fullscreen-triangle vertex + output struct are inlined here
// (classic injected them via a shared shader prelude we don't carry).
static constexpr const char* kSplashWGSL = R"(
struct VOut { @builtin(position) position: vec4f, @location(0) uv: vec2f };

@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> VOut {
    // One oversized triangle covering the viewport; uv (0,0) at the TOP-left so the
    // logo geometry below (nodes near y=0.25) reads the same way up as the app icon.
    var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    var o: VOut;
    o.position = vec4f(p[vi], 0.0, 1.0);
    o.uv = vec2f(p[vi].x * 0.5 + 0.5, 1.0 - (p[vi].y * 0.5 + 0.5));
    return o;
}

@group(0) @binding(0) var<uniform> u: vec4f; // x=time, y=aspect, z=seed

// Logo V geometry: 5 nodes, 4 edges (normalized 0-1 coords)
const NODE0 = vec2f(0.300, 0.250);
const NODE1 = vec2f(0.380, 0.460);
const NODE2 = vec2f(0.500, 0.780);
const NODE3 = vec2f(0.620, 0.460);
const NODE4 = vec2f(0.700, 0.250);
const NODE_R = array<f32, 5>(0.028, 0.022, 0.030, 0.022, 0.028);

fn hash(p: vec2f) -> f32 {
    var h = dot(p, vec2f(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

fn noise(p: vec2f) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u2 = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i + vec2f(0.0, 0.0)), hash(i + vec2f(1.0, 0.0)), u2.x),
               mix(hash(i + vec2f(0.0, 1.0)), hash(i + vec2f(1.0, 1.0)), u2.x), u2.y);
}

fn fbm(p_in: vec2f) -> f32 {
    var p = p_in;
    var v = 0.0;
    var a = 0.5;
    let shift = vec2f(100.0);
    let rot = mat2x2f(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
    for (var i = 0; i < 5; i++) {
        v += a * noise(p);
        p = rot * p * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

// Distance from point p to segment a->b. Returns (distance, t along segment).
fn seg_dist(p: vec2f, a: vec2f, b: vec2f) -> vec2f {
    let ab = b - a;
    let t = clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
    let closest = a + ab * t;
    return vec2f(length(p - closest), t);
}

// Logo edge layer: flowing noise textured along each edge, masked by distance.
fn logo_edges(p: vec2f, time: f32) -> vec2f {
    let nodes = array<vec2f, 5>(NODE0, NODE1, NODE2, NODE3, NODE4);
    let t_offsets = array<f32, 4>(0.0, 1.0, 2.0, 3.0);
    let edge_half_width = 0.028;
    let soft_edge = 0.040;

    var brightness = 0.0;
    var min_d = 1.0;

    for (var i = 0; i < 4; i++) {
        let a = nodes[i];
        let b = nodes[i + 1];
        let sd = seg_dist(p, a, b);
        let d = sd.x;
        let t = sd.y;
        min_d = min(min_d, d);

        let mask = smoothstep(edge_half_width + soft_edge, edge_half_width * 0.3, d);

        let flow_coord = (t_offsets[i] + t) * 4.0 - time * 0.35;
        let cross_coord = d * 20.0;
        let flow_noise = fbm(vec2f(flow_coord, cross_coord) + vec2f(f32(i) * 7.3) + vec2f(u.z * 0.7));

        brightness = max(brightness, mask * flow_noise);
    }

    return vec2f(brightness, min_d);
}

@fragment fn fs_main(in: VOut) -> @location(0) vec4f {
    let t = u.x;
    let aspect = u.y;
    let seed = u.z;
    var uv = in.uv;
    uv.x *= aspect;

    let seed_off = vec2f(seed, seed * 1.7 + 3.1);

    // --- Base nebula ---
    let warp = vec2f(
        fbm(uv * 3.0 + vec2f(t * 0.08, t * 0.06) + seed_off),
        fbm(uv * 3.0 + vec2f(t * -0.05, t * 0.09) + vec2f(5.2, 1.3) + seed_off)
    );
    let n = fbm(uv * 2.0 + warp * 1.5 + vec2f(t * 0.02) + seed_off);

    let d = length(in.uv - vec2f(0.5));
    let vignette = 1.0 - smoothstep(0.1, 0.85, d);

    let deep    = vec3f(0.02, 0.02, 0.04);
    let blue    = vec3f(0.06, 0.10, 0.22);
    let purple  = vec3f(0.12, 0.06, 0.18);
    let teal    = vec3f(0.04, 0.14, 0.16);

    var color = deep;
    color = mix(color, blue,   smoothstep(0.25, 0.55, n) * vignette);
    color = mix(color, purple, smoothstep(0.45, 0.70, warp.x) * vignette * 0.6);
    color = mix(color, teal,   smoothstep(0.50, 0.75, warp.y) * vignette * 0.4);

    let wisp = smoothstep(0.62, 0.72, n) * vignette * vignette;
    color += vec3f(0.08, 0.10, 0.15) * wisp;

    // --- Logo V overlay ---
    let logo = logo_edges(in.uv, t);
    let edge_bright = logo.x;

    let logo_cyan = vec3f(0.12, 0.40, 0.55);
    let logo_blue = vec3f(0.10, 0.30, 0.58);
    let edge_color = mix(logo_cyan, logo_blue, smoothstep(0.3, 0.7, in.uv.y));
    let edge_modulated = edge_bright * mix(0.5, 1.0, n);
    color += edge_color * edge_modulated * vignette * 0.30;

    // Node glow spots
    let node_positions = array<vec2f, 5>(NODE0, NODE1, NODE2, NODE3, NODE4);
    for (var i = 0; i < 5; i++) {
        let nd = length(in.uv - node_positions[i]);
        let r = NODE_R[i];
        let core = smoothstep(r, r * 0.2, nd);
        let halo = smoothstep(r * 4.0, r * 0.5, nd);
        let node_color = mix(logo_cyan, vec3f(0.20, 0.55, 0.68), core);
        color += node_color * (core * 0.20 + halo * 0.05) * vignette;
    }

    return vec4f(color, 1.0);
}
)";

bool Splash::init(WGPUDevice device, WGPUTextureFormat surface_format, uint32_t msaa) {
    std::random_device rd;
    seed_ = std::uniform_real_distribution<float>(0.0f, 100.0f)(rd);

    WGPUShaderSourceWGSL wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = to_sv(kSplashWGSL);
    WGPUShaderModuleDescriptor sm_desc{};
    sm_desc.nextInChain = &wgsl.chain;
    sm_desc.label = to_sv("Splash Shader");
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &sm_desc);
    if (!shader) { std::fprintf(stderr, "[vivid] splash: shader module failed\n"); return false; }

    WGPUBindGroupLayoutEntry bgl_entry{};
    bgl_entry.binding = 0;
    bgl_entry.visibility = WGPUShaderStage_Fragment;
    bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
    bgl_entry.buffer.minBindingSize = 16;
    WGPUBindGroupLayoutDescriptor bgl_desc{};
    bgl_desc.label = to_sv("Splash BGL");
    bgl_desc.entryCount = 1;
    bgl_desc.entries = &bgl_entry;
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bgl_desc);

    WGPUPipelineLayoutDescriptor pl_desc{};
    pl_desc.label = to_sv("Splash PL");
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &pl_desc);

    WGPUBufferDescriptor ub_desc{};
    ub_desc.label = to_sv("Splash Uniforms");
    ub_desc.size = 16;
    ub_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniform_buf_ = wgpuDeviceCreateBuffer(device, &ub_desc);

    WGPUColorTargetState color_target{};
    color_target.format = surface_format;
    color_target.writeMask = WGPUColorWriteMask_All;   // opaque (nebula fills the frame)
    WGPUFragmentState fragment{};
    fragment.module = shader;
    fragment.entryPoint = to_sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor rp_desc{};
    rp_desc.label = to_sv("Splash Pipeline");
    rp_desc.layout = layout;
    rp_desc.vertex.module = shader;
    rp_desc.vertex.entryPoint = to_sv("vs_main");
    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
    rp_desc.primitive.cullMode = WGPUCullMode_None;
    rp_desc.multisample.count = msaa;   // must match the frame's MSAA color target
    rp_desc.multisample.mask = 0xFFFFFFFF;
    rp_desc.fragment = &fragment;
    pipeline_ = wgpuDeviceCreateRenderPipeline(device, &rp_desc);

    if (pipeline_) {
        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer = uniform_buf_;
        bg_entry.size = 16;
        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = to_sv("Splash BG");
        bg_desc.layout = bgl;
        bg_desc.entryCount = 1;
        bg_desc.entries = &bg_entry;
        bind_group_ = wgpuDeviceCreateBindGroup(device, &bg_desc);
    } else {
        std::fprintf(stderr, "[vivid] splash: pipeline failed\n");
    }

    wgpuPipelineLayoutRelease(layout);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuShaderModuleRelease(shader);
    return pipeline_ != nullptr && bind_group_ != nullptr;
}

void Splash::render(GpuContext& gpu, ui::Renderer2D& ui, GLFWwindow* window, const char* status) {
    if (!pipeline_ || !bind_group_) return;
    FrameState frame;
    if (!gpu.begin_frame(frame)) return;

    if (start_time_ == 0.0) start_time_ = glfwGetTime();

    int win_w = 0, win_h = 0, fb_w = 0, fb_h = 0;
    glfwGetWindowSize(window, &win_w, &win_h);
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    const float wf = static_cast<float>(win_w), hf = static_cast<float>(win_h);
    const float aspect = (hf > 0.0f) ? wf / hf : 1.0f;
    const float elapsed = static_cast<float>(glfwGetTime() - start_time_);
    const float uniforms[4] = { elapsed, aspect, seed_, 0.0f };
    wgpuQueueWriteBuffer(gpu.queue(), uniform_buf_, 0, uniforms, sizeof(uniforms));

    // 1. Nebula + logo shader — a fullscreen triangle into the frame's MSAA view (the
    // MSAA resolve to the surface happens in end_frame, like every other frame).
    {
        WGPURenderPassColorAttachment att{};
        att.view = frame.view;
        att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        att.loadOp = WGPULoadOp_Clear;
        att.storeOp = WGPUStoreOp_Store;
        att.clearValue = { 0.01, 0.01, 0.02, 1.0 };
        WGPURenderPassDescriptor rp{};
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &att;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &rp);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group_, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    // 2. Centered info panel: title / version / copyright / status (overlaid via Renderer2D).
    const float cx = wf * 0.5f, cy = hf * 0.5f;
    const float pw = 320.0f, ph = 150.0f;
    const float px = cx - pw * 0.5f, py = cy - ph * 0.5f;
    ui.draw_rounded_rect(px, py, pw, ph, 8.0f, 0.0f, 0.0f, 0.0f, 0.55f);

    const char* title = "Vivid";
    const float title_scale = 1.6f;
    ui.draw_text(cx - ui.text_width(title, title_scale) * 0.5f, py + 26.0f,
                 title, 0.85f, 0.88f, 0.92f, 1.0f, title_scale);

    const float info_scale = 0.85f;
    const float lh = ui.line_height() * info_scale;
    char version_str[64];
    std::snprintf(version_str, sizeof version_str, "v%s", VIVID_VERSION);
    ui.draw_text(cx - ui.text_width(version_str, info_scale) * 0.5f, py + 58.0f,
                 version_str, 0.5f, 0.53f, 0.58f, 0.8f, info_scale);
    const char* copyright = "\xC2\xA9 2025-2026 Jeff Crouse";
    ui.draw_text(cx - ui.text_width(copyright, info_scale) * 0.5f, py + 58.0f + lh + 4.0f,
                 copyright, 0.4f, 0.42f, 0.45f, 0.6f, info_scale);

    const float sep_y = py + ph - 38.0f;
    ui.draw_rect(px + 20.0f, sep_y, pw - 40.0f, 1.0f, 0.3f, 0.32f, 0.35f, 0.25f);

    if (status && *status) {
        const float status_scale = 0.8f;
        ui.draw_text(cx - ui.text_width(status, status_scale) * 0.5f, sep_y + 12.0f,
                     status, 0.45f, 0.48f, 0.52f, 0.7f, status_scale);
    }
    ui.flush(frame.encoder, frame.view,
             static_cast<uint32_t>(win_w), static_cast<uint32_t>(win_h),
             static_cast<uint32_t>(fb_w), static_cast<uint32_t>(fb_h));

    gpu.end_frame(frame);
    glfwPollEvents();
}

void Splash::shutdown() {
    if (bind_group_)  { wgpuBindGroupRelease(bind_group_); bind_group_ = nullptr; }
    if (uniform_buf_) { wgpuBufferRelease(uniform_buf_); uniform_buf_ = nullptr; }
    if (pipeline_)    { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
}

}  // namespace vivid
