#include "gpu/shader_file_op.h"

#include "operator_api/gpu_common.h"

#include <cstdio>

namespace vivid {

// ---------------------------------------------------------------------------
// ShaderDef
// ---------------------------------------------------------------------------

void ShaderDef::finalize() {
    params = host_params(meta);
    layout = uniform_layout(meta);
    choice_ptrs.assign(params.size(), {});
    for (size_t i = 0; i < params.size(); ++i)
        for (const std::string& c : params[i].choices)
            choice_ptrs[i].push_back(c.c_str());
}

// The INTERFACE is the ports and the params — what the graph, the inspector, wires, mappings
// and the saved project all see. Two defs that agree on it are interchangeable under a live
// node (a body edit); two that don't need the node instances rebuilt.
bool ShaderDef::same_interface(const ShaderDef& o) const {
    if (meta.name != o.meta.name || meta.inputs != o.meta.inputs) return false;
    if (params.size() != o.params.size()) return false;
    for (size_t i = 0; i < params.size(); ++i) {
        const ShaderHostParam &a = params[i], &b = o.params[i];
        if (a.name != b.name || a.type != b.type || a.display != b.display) return false;
        if (a.def != b.def || a.min != b.min || a.max != b.max) return false;
        if (a.choices != b.choices || a.label != b.label || a.description != b.description) return false;
        if (a.group != b.group) return false;
    }
    return layout.size == o.layout.size;
}

// ---------------------------------------------------------------------------
// ShaderFileOp
// ---------------------------------------------------------------------------

namespace {

VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name;
    p.type = VIVID_PORT_TEXTURE;
    p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE;
    p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}

// The body used when the shader's own body will not compile. A generator renders black
// and a filter passes its input through — never garbage, and never a black screen mid-set
// just because someone saved a syntax error (ADR-0016 / S3).
std::string fallback_body(const ShaderMeta& m) {
    if (m.inputs.empty())
        return "@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {\n"
               "    return vec4f(0.0, 0.0, 0.0, 1.0);\n}\n";
    return "@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {\n"
           "    return textureSample(" + m.inputs[0] + ", samp, inp.uv);\n}\n";
}

}  // namespace

ShaderFileOp::ShaderFileOp(std::shared_ptr<ShaderSlot> slot)
    : slot_(std::move(slot)), def_(slot_->def), gen_(slot_->generation) {
    const ShaderDef& d = *def_;

    params_.reserve(d.params.size());   // reserved: collect_params() hands out stable pointers
    for (size_t i = 0; i < d.params.size(); ++i) {
        const ShaderHostParam& hp = d.params[i];
        ParamBase p{};
        // Every string here points into the ShaderDef, which outlives both this instance and
        // the registry's cached descriptor. See the comment on ShaderDef.
        p.name = hp.name.c_str();
        p.type = hp.type;
        p.default_value = hp.def;
        p.min_value = hp.min;
        p.max_value = hp.max;
        p.value = hp.def;
        p.display_hint = hp.display;
        if (!hp.group.empty())       p.group = hp.group.c_str();
        if (!hp.description.empty()) p.description = hp.description.c_str();
        if (!d.choice_ptrs[i].empty()) {
            p.choice_labels = const_cast<const char**>(d.choice_ptrs[i].data());
            p.choice_count = static_cast<uint32_t>(d.choice_ptrs[i].size());
        }
        params_.push_back(p);
    }

    for (const std::string& in : d.meta.inputs) ports_.push_back(tex_port(in.c_str(), VIVID_PORT_INPUT));
    ports_.push_back(tex_port("texture", VIVID_PORT_OUTPUT));

    ubo_staging_.resize(d.layout.size);
}

ShaderFileOp::~ShaderFileOp() {
    if (bg_) wgpuBindGroupRelease(bg_);
    if (samp_) wgpuSamplerRelease(samp_);
    if (ubo_) wgpuBufferRelease(ubo_);
    release_pipeline(pipe_);
    release_pipeline(fallback_);
    if (pl_) wgpuPipelineLayoutRelease(pl_);
    if (bgl_) wgpuBindGroupLayoutRelease(bgl_);
}

void ShaderFileOp::release_pipeline(WGPURenderPipeline& p) {
    if (p) { wgpuRenderPipelineRelease(p); p = nullptr; }
}

void ShaderFileOp::collect_params(std::vector<ParamBase*>& out) {
    for (auto& p : params_) out.push_back(&p);
}

void ShaderFileOp::collect_ports(std::vector<VividPortDescriptor>& out) {
    for (const auto& p : ports_) out.push_back(p);
}

WGPURenderPipeline ShaderFileOp::compile(const VividGpuContext* c, const std::string& body,
                                          const char* label, std::string& err) {
    const std::string src = generate_prelude(def_->meta) + body;
    WGPUShaderModule sh = vivid::gpu::create_shader_checked(c->device, src.c_str(), label, err);
    if (!sh || !err.empty()) {
        if (sh) wgpuShaderModuleRelease(sh);
        return nullptr;
    }
    WGPURenderPipeline p = vivid::gpu::create_pipeline(c->device, sh, pl_, c->output_format, label);
    wgpuShaderModuleRelease(sh);   // the pipeline holds its own reference
    return p;
}

// A body edit: recompile against the SAME bindings and uniform layout (the interface is
// unchanged by construction — see ShaderDef::same_interface). A failed recompile keeps the
// last-good pipeline: saving a syntax error mid-performance must never black out a live output.
bool ShaderFileOp::rebuild_pipeline(const VividGpuContext* c) {
    std::string err;
    WGPURenderPipeline next = compile(c, def_->meta.body, def_->meta.name.c_str(), err);
    if (!next) {
        error_ = err.empty() ? "shader failed to compile" : err;
        std::fprintf(stderr, "[vivid] shader '%s' (%s): %s — keeping the last good version\n",
                     def_->meta.name.c_str(), def_->path.c_str(), error_.c_str());
        return false;
    }
    if (pipe_) wgpuRenderPipelineRelease(pipe_);
    pipe_ = next;
    error_.clear();
    std::fprintf(stderr, "[vivid] shader '%s' reloaded\n", def_->meta.name.c_str());
    return true;
}

bool ShaderFileOp::build(const VividGpuContext* c) {
    const ShaderDef& d = *def_;
    const uint32_t nin = static_cast<uint32_t>(d.meta.inputs.size());
    const char* label = d.meta.name.c_str();

    // The bind group is generated from the same declaration as the WGSL: uniform at 0, one
    // texture per declared input, then the sampler. They cannot drift apart.
    ubo_ = vivid::gpu::create_uniform_buffer(c->device, d.layout.size, label);

    std::vector<WGPUBindGroupLayoutEntry> e(1 + nin + (nin ? 1 : 0));
    e[0].binding = 0;
    e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    e[0].buffer.type = WGPUBufferBindingType_Uniform;
    e[0].buffer.minBindingSize = d.layout.size;
    for (uint32_t i = 0; i < nin; ++i) {
        e[1 + i].binding = 1 + i;
        e[1 + i].visibility = WGPUShaderStage_Fragment;
        e[1 + i].texture.sampleType = WGPUTextureSampleType_Float;
        e[1 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }
    if (nin) {
        e[1 + nin].binding = 1 + nin;
        e[1 + nin].visibility = WGPUShaderStage_Fragment;
        e[1 + nin].sampler.type = WGPUSamplerBindingType_Filtering;
    }
    WGPUBindGroupLayoutDescriptor ld{};
    ld.entryCount = static_cast<uint32_t>(e.size());
    ld.entries = e.data();
    bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);

    WGPUPipelineLayoutDescriptor pld{};
    pld.bindGroupLayoutCount = 1;
    pld.bindGroupLayouts = &bgl_;
    pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);

    if (nin) {
        WGPUSamplerDescriptor sd{};
        sd.magFilter = WGPUFilterMode_Linear;
        sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge;
        sd.addressModeV = WGPUAddressMode_ClampToEdge;
        sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
    }

    std::string err;
    pipe_ = compile(c, d.meta.body, label, err);
    if (!pipe_) {
        error_ = err.empty() ? "shader failed to compile" : err;
        std::fprintf(stderr, "[vivid] shader '%s' (%s): %s\n", label, d.path.c_str(), error_.c_str());
        std::string ferr;
        fallback_ = compile(c, fallback_body(d.meta), label, ferr);   // black / passthrough
        return false;
    }
    return true;
}

void ShaderFileOp::process_gpu(const VividGpuContext* c) {
    if (!tried_) { tried_ = true; build(c); }

    // The file changed under us. Adopt the new version only if it declares the SAME interface;
    // if it doesn't, the library is rebuilding this node from scratch anyway, and adopting a
    // def whose param count no longer matches our ParamBase storage would corrupt the packing.
    if (slot_->generation != gen_) {
        gen_ = slot_->generation;
        std::shared_ptr<const ShaderDef> next = slot_->def;
        if (next && def_ && next->same_interface(*def_)) {
            def_ = std::move(next);
            rebuild_pipeline(c);
        }
    }

    WGPURenderPipeline pipe = pipe_ ? pipe_ : fallback_;
    if (!pipe) return;

    const ShaderDef& d = *def_;
    pack_uniforms(d.meta, d.layout, c->param_values,
                  c->param_values ? params_.size() : 0,
                  static_cast<float>(c->output_width), static_cast<float>(c->output_height),
                  static_cast<float>(c->time),
                  ubo_staging_.data(), ubo_staging_.size());
    wgpuQueueWriteBuffer(c->queue, ubo_, 0, ubo_staging_.data(), ubo_staging_.size());

    // Rebuilt each frame: the views change as the graph is rewired.
    const uint32_t nin = static_cast<uint32_t>(d.meta.inputs.size());
    std::vector<WGPUBindGroupEntry> be(1 + nin + (nin ? 1 : 0));
    be[0].binding = 0;
    be[0].buffer = ubo_;
    be[0].size = d.layout.size;
    for (uint32_t i = 0; i < nin; ++i) {
        be[1 + i].binding = 1 + i;
        be[1 + i].textureView = (i < c->input_texture_count && c->input_texture_views[i])
                                    ? c->input_texture_views[i]
                                    : c->output_texture_view;   // unwired: sample our own RT (black)
    }
    if (nin) {
        be[1 + nin].binding = 1 + nin;
        be[1 + nin].sampler = samp_;
    }
    WGPUBindGroupDescriptor bd{};
    bd.layout = bgl_;
    bd.entryCount = static_cast<uint32_t>(be.size());
    bd.entries = be.data();
    if (bg_) wgpuBindGroupRelease(bg_);
    bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);

    vivid::gpu::run_pass(c->command_encoder, pipe, bg_, c->output_texture_view, d.meta.name.c_str());
}

}  // namespace vivid
