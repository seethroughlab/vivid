// P2.0 spike operator — a standalone GPU generator compiled to a loadable .dylib.
//
// The entire point of this file is to PROVE the dlopen boundary end-to-end:
//   - authored as a plain `struct : OperatorBase, GpuProcessable` + VIVID_REGISTER,
//     compiled WITHOUT the codegen tool (the macro emits the extern "C" surface);
//   - it issues wgpu calls (CreateShaderModule/Pipeline/render pass) on the HOST's
//     WGPUDevice, handed in via VividGpuContext — so host and operator must share
//     one wgpu-native instance (the dylib links the same shared libwgpu_native.dylib).
//
// It is self-contained against operator_api/ ONLY (no app/src/gpu/* link), so it
// stands in for any future externally-authored operator package.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <array>
#include <string>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}

const char* kSolidWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U { hue: f32, p0: f32, p1: f32, p2: f32 };
@group(0) @binding(0) var<uniform> u: U;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let c = 0.5 + 0.5 * cos(vec3f(0.0, 2.0, 4.0) + u.hue * 6.2831853);
    return vec4f(c, 1.0);
}
)";
}  // namespace

// A solid-color fullscreen generator with one param (hue). Lives in the global
// namespace so VIVID_REGISTER's extern "C" exports are unmangled.
struct SpikeSolidOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "SpikeSolid";
    static constexpr const char* kDisplayName = "Spike Solid";
    static constexpr const char* kSummary = "Example loadable .dylib operator: a solid hue-coloured fill.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "solid", "example"};
    vivid::Param<float> hue{"hue", 0.5f, 0.f, 1.f};

    bool tried_ = false;
    WGPUShaderModule    sh_   = nullptr;
    WGPUBindGroupLayout bgl_  = nullptr;
    WGPUPipelineLayout  pl_   = nullptr;
    WGPURenderPipeline  pipe_ = nullptr;
    WGPUBuffer          ubo_  = nullptr;
    WGPUBindGroup       bg_   = nullptr;

    ~SpikeSolidOp() override {
        if (bg_)   wgpuBindGroupRelease(bg_);
        if (ubo_)  wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_);
        if (pl_)   wgpuPipelineLayoutRelease(pl_);
        if (bgl_)  wgpuBindGroupLayoutRelease(bgl_);
        if (sh_)   wgpuShaderModuleRelease(sh_);
    }

    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&hue); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }

    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kSolidWGSL, "SpikeSolid", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 16, "SpikeSolid U");
        WGPUBindGroupLayoutEntry e{};
        e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 16;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "SpikeSolid Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 16;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }

    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        float u[4] = { c->param_values ? c->param_values[0] : hue.value, 0.f, 0.f, 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "SpikeSolid");
    }
};

VIVID_REGISTER(SpikeSolidOp)
