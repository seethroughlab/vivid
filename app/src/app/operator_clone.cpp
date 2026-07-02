#include "app/operator_clone.h"
#include "platform/platform.h"
#include "packages/package_manager.h"
#include "gpu/operator_scan.h"
#include "gpu/operator_loader.h"
#include "gpu/op_runtime.h"

#include <filesystem>
#include <fstream>

namespace vivid {
namespace {

// A package-operator .cpp that reproduces the built-in "Plasma" generator (its GLSL
// translated to WGSL). `__OPNAME__` is substituted with the unique clone type name.
// Mirrors app/operators/packages/example-visuals/gradient.cpp (the proven package shape).
const char* kPlasmaCloneTemplate = R"SRC(// Editable clone of the built-in "Plasma" generator (WGSL package operator).
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
const char* kWGSL = R"WGSL(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U { res: vec2f, time: f32, warp: f32, hue: f32, density: f32, glow: f32, pad: f32 };
@group(0) @binding(0) var<uniform> u: U;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let t = u.time;
    let dens = 6.0 + u.density * 18.0;
    let w = inp.uv + u.warp * 0.3 * vec2f(sin(inp.uv.y * 8.0 + t), cos(inp.uv.x * 8.0 + t));
    let v = sin(w.x * dens + t) + sin(w.y * dens + t * 1.3)
          + sin((w.x + w.y) * dens * 0.6 + t * 0.7)
          + sin(length(w - vec2f(0.5)) * dens * 1.8 - t * 2.0);
    let col = 0.5 + 0.5 * cos(vec3f(0.0, 2.0, 4.0) + v + u.hue * 6.2831853);
    return vec4f(col * (0.6 + u.glow), 1.0);
}
)WGSL";
}  // namespace

struct __OPNAME__ : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "__OPNAME__";
    static constexpr const char* kDisplayName = "__OPNAME__";
    static constexpr const char* kSummary = "Editable clone of the built-in Plasma generator.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "plasma", "clone"};
    vivid::Param<float> warp   {"warp",    0.5f, 0.f, 1.f};
    vivid::Param<float> hue    {"hue",     0.0f, 0.f, 1.f};
    vivid::Param<float> density{"density", 0.5f, 0.f, 1.f};
    vivid::Param<float> glow   {"glow",    0.5f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr;
    WGPUPipelineLayout pl_ = nullptr; WGPURenderPipeline pipe_ = nullptr;
    WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~__OPNAME__() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&warp); o.push_back(&hue); o.push_back(&density); o.push_back(&glow);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kWGSL, "PlasmaClone", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "PlasmaClone U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0;
        e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 32;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "PlasmaClone Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 32;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values;
        float u[8] = { float(c->output_width), float(c->output_height), float(c->time),
                       p ? p[0] : warp.value, p ? p[1] : hue.value, p ? p[2] : density.value, p ? p[3] : glow.value, 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "PlasmaClone");
    }
};
VIVID_REGISTER(__OPNAME__)
)SRC";

void replace_all(std::string& s, const std::string& from, const std::string& to) {
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size()))
        s.replace(p, from.size(), to);
}
bool write_file(const std::filesystem::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::trunc);
    if (!out) return false;
    out << s; return static_cast<bool>(out);
}

}  // namespace

bool operator_has_clone_template(const std::string& builtin_name) {
    return builtin_name == "Plasma";
}

std::string clone_operator_source(const std::string& builtin_name, const std::string& type_name) {
    if (!operator_has_clone_template(builtin_name)) return {};
    std::string src = kPlasmaCloneTemplate;
    replace_all(src, "__OPNAME__", type_name);
    return src;
}

std::string clone_operator_manifest(const std::string& type_name) {
    return "{\n  \"name\": \"" + type_name + "\",\n  \"version\": \"0.1.0\",\n"
           "  \"operators\": [ { \"name\": \"" + type_name + "\", \"source\": \"" +
           type_name + ".cpp\", \"gpu\": true } ]\n}\n";
}

CloneResult clone_operator(OpRegistry& reg_cat, std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                           const std::string& builtin) {
    namespace fs = std::filesystem;
    if (!operator_has_clone_template(builtin))
        return { false, "", "", "no clone template for '" + builtin + "'" };

    // A unique, C++-identifier-safe type name not already registered.
    std::string name = builtin + "Clone";
    for (int n = 2; reg_cat.has(name); ++n) name = builtin + "Clone" + std::to_string(n);

    const std::string data = platform::user_data_dir();
    if (data.empty()) return { false, "", "", "no user data dir" };
    const fs::path pkg = fs::path(data) / "clones" / name;
    std::error_code ec; fs::create_directories(pkg, ec);

    const std::string src = clone_operator_source(builtin, name);
    const fs::path srcpath = pkg / (name + ".cpp");
    if (!write_file(srcpath, src)) return { false, "", srcpath.string(), "write source failed" };
    if (!write_file(pkg / "vivid-package.json", clone_operator_manifest(name)))
        return { false, "", srcpath.string(), "write manifest failed" };

    // Compile (clang++) into the managed operators dir, then register the dylib live.
    PackageInstallResult ir = install_package(pkg.string());
    if (!ir.ok) return { false, "", srcpath.string(), ir.error.empty() ? "install failed" : ir.error };
    if (ir.compiles.empty() || !ir.compiles[0].success)
        return { false, "", srcpath.string(),
                 ir.compiles.empty() ? "no compile output" : ir.compiles[0].error_output };
    const std::string reg = load_and_register_operator(ir.compiles[0].dylib_path, reg_cat, loaders);
    if (reg.empty()) return { false, "", srcpath.string(), "register failed (abi mismatch?)" };
    return { true, reg, srcpath.string(), "" };
}

}  // namespace vivid
