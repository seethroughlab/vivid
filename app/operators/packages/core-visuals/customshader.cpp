// Core visual package operator: CustomShader — data-driven GLSL generator loaded from
// a .glsl file. Migrated from the built-in CustomShaderOp (which used the host ShaderOp
// + AssetShader). Here the fragment source comes from a Param<FilePath> (file-param
// channel), and the GLSL pipeline (ShaderOp's contract) is replicated inline via
// wgpu-native's WGPUShaderSourceGLSL. The .glsl must follow the contract: v_uv in /
// o_color out + the u_res/u_time/u_warp/u_hue/u_density/u_glow uniform block.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <webgpu/wgpu.h>   // native extension: WGPUShaderSourceGLSL

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
// std140: res@0, time@8, warp/hue/density/glow @12.., padded to 32.
struct Uniforms { float res[2]; float time; float u[4]; float _pad; };
const char* kVertGLSL = R"(#version 450
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
WGPUShaderModule make_glsl(WGPUDevice d, WGPUShaderStage stage, const char* src, const char* label) {
    WGPUShaderSourceGLSL g{};
    g.chain.sType = static_cast<WGPUSType>(WGPUSType_ShaderSourceGLSL);
    g.stage = stage; g.code = vivid_sv(src); g.defineCount = 0; g.defines = nullptr;
    WGPUShaderModuleDescriptor desc{}; desc.nextInChain = &g.chain; desc.label = vivid_sv(label);
    return wgpuDeviceCreateShaderModule(d, &desc);
}
}  // namespace

struct CustomShaderOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "CustomShader";
    static constexpr const char* kDisplayName = "Custom Shader";
    static constexpr const char* kSummary = "Data-driven GLSL generator: renders a project .glsl file (pick it in `file`).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "shader", "glsl"};
    vivid::Param<vivid::FilePath> file{"file", ""};
    CustomShaderOp() { vivid::asset_kind(file, "glsl"); }   // ADR-0021/P3: dialog/drop filter
    vivid::Param<float> p0{"warp", 0.5f, 0.f, 1.f};
    vivid::Param<float> p1{"hue", 0.0f, 0.f, 1.f};
    vivid::Param<float> p2{"density", 0.5f, 0.f, 1.f};
    vivid::Param<float> p3{"glow", 0.5f, 0.f, 1.f};
    std::string loaded_ = "\x01";   // sentinel so an empty path is handled once
    uint64_t loaded_generation_ = 0;
    bool failed_ = false;
    WGPUShaderModule vert_ = nullptr, frag_ = nullptr;
    WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;

    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&file); o.push_back(&p0); o.push_back(&p1); o.push_back(&p2); o.push_back(&p3);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }

    ~CustomShaderOp() override { release_pipeline(); }
    void release_pipeline() {
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        if (ubo_) { wgpuBufferRelease(ubo_); ubo_ = nullptr; }
        if (pipe_) { wgpuRenderPipelineRelease(pipe_); pipe_ = nullptr; }
        if (pl_) { wgpuPipelineLayoutRelease(pl_); pl_ = nullptr; }
        if (bgl_) { wgpuBindGroupLayoutRelease(bgl_); bgl_ = nullptr; }
        if (frag_) { wgpuShaderModuleRelease(frag_); frag_ = nullptr; }
        if (vert_) { wgpuShaderModuleRelease(vert_); vert_ = nullptr; }
    }
    void reload(const VividGpuContext* c) {
        release_pipeline();
        loaded_ = file.str_value;
        loaded_generation_ = c ? c->file_param_generation : 0;
        failed_ = true;   // pessimistic until compiled
        if (file.str_value.empty()) return;
        std::ifstream f(file.str_value, std::ios::binary);
        if (!f) { std::fprintf(stderr, "[CustomShader] cannot open %s\n", file.str_value.c_str()); return; }
        std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (src.empty()) return;
        vert_ = make_glsl(c->device, WGPUShaderStage_Vertex, kVertGLSL, "cs.vert");
        frag_ = make_glsl(c->device, WGPUShaderStage_Fragment, src.c_str(), "cs.frag");
        if (!vert_ || !frag_) return;
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = sizeof(Uniforms);
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        WGPUBufferDescriptor bd{}; bd.size = sizeof(Uniforms); bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        ubo_ = wgpuDeviceCreateBuffer(c->device, &bd);
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = sizeof(Uniforms);
        WGPUBindGroupDescriptor bgd{}; bgd.layout = bgl_; bgd.entryCount = 1; bgd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bgd);
        WGPUColorTargetState ct{}; ct.format = c->output_format; ct.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs{}; fs.module = frag_; fs.entryPoint = vivid_sv("main"); fs.targetCount = 1; fs.targets = &ct;
        WGPURenderPipelineDescriptor pd{}; pd.layout = pl_;
        pd.vertex.module = vert_; pd.vertex.entryPoint = vivid_sv("main"); pd.vertex.bufferCount = 0;
        pd.fragment = &fs; pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pd.primitive.cullMode = WGPUCullMode_None; pd.primitive.frontFace = WGPUFrontFace_CCW;
        pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFFu;
        pipe_ = wgpuDeviceCreateRenderPipeline(c->device, &pd);
        failed_ = (pipe_ == nullptr);
        if (failed_) std::fprintf(stderr, "[CustomShader] compile failed: %s\n", file.str_value.c_str());
    }
    void process_gpu(const VividGpuContext* c) override {
        const uint64_t generation = c ? c->file_param_generation : 0;
        if (file.str_value != loaded_ || generation != loaded_generation_) reload(c);
        if (failed_ || !pipe_) return;
        const float* p = c->param_values;   // file, warp, hue, density, glow (file slot ignored here)
        Uniforms u{}; u.res[0] = float(c->output_width); u.res[1] = float(c->output_height); u.time = float(c->time);
        u.u[0] = p ? p[1] : p0.value; u.u[1] = p ? p[2] : p1.value; u.u[2] = p ? p[3] : p2.value; u.u[3] = p ? p[4] : p3.value;
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, &u, sizeof u);
        WGPURenderPassColorAttachment att{}; att.view = c->output_texture_view; att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        att.loadOp = WGPULoadOp_Clear; att.storeOp = WGPUStoreOp_Store; att.clearValue = WGPUColor{ 0, 0, 0, 1 };
        WGPURenderPassDescriptor rp{}; rp.colorAttachmentCount = 1; rp.colorAttachments = &att;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(c->command_encoder, &rp);
        wgpuRenderPassEncoderSetPipeline(pass, pipe_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg_, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
};

VIVID_REGISTER(CustomShaderOp)

// ADR-0021/P3: drop a .glsl onto the graph -> a CustomShader node rendering it.
static const char* const kCustomShaderDropExts[] = { ".glsl", ".frag" };
static const VividFileDropHandlerDescriptor kCustomShaderDrop[] = {
    { "CustomShader", kCustomShaderDropExts, 2, "file", 6, "Render as a GLSL shader" }
};
VIVID_FILE_DROP(kCustomShaderDrop)
