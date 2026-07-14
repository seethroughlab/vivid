#pragma once

// ADR-0016 / S3 — the operator a SHADER FILE becomes.
//
// One `.wgsl`/`.glsl` file in the library = one operator type in `OpRegistry`, whose
// params are the ones its header declares: wireable, mappable, inspectable and persisted
// exactly like a compiled operator's. Nothing about a shader node is special downstream —
// which is why shaders appear in the Tab chooser and in `list_operators` for free.
//
// (Not to be confused with `gpu/shader_op.h`, which is the older fixed-four-uniform GLSL
// pass behind the `CustomShader` node. That one keeps its hardcoded contract; this is the
// one that lets a file declare its own.)

#include "gpu/op_runtime.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/operator.h"
#include "operator_api/shader_meta.h"

#include <memory>
#include <string>
#include <vector>

#include <webgpu/webgpu.h>

namespace vivid {

// The immortal, shared definition of ONE shader file — parsed once at scan time and
// captured by the registry factory, so every instance shares it.
//
// It must outlive every instance AND every cached descriptor: `ParamBase::name`,
// `group`, `description` and `choice_labels` are raw `const char*`, and the registry
// builds its per-type descriptor from a TEMPORARY instance. Pointing those at strings
// owned by the instance would leave the cached descriptor dangling the moment that temp
// died. They point in here instead.
struct ShaderDef {
    ShaderMeta                   meta;
    std::vector<ShaderHostParam> params;      // the declared params, expanded (color -> r/g/b, ...)
    UniformLayout                layout;
    std::string                  path;        // the file on disk (hot-reload, fork-to-edit)
    std::string                  tier;        // "user" | "project" | "bundled"

    // Stable const char* arrays for the enum params' choice labels.
    std::vector<std::vector<const char*>> choice_ptrs;   // parallel to params

    // Build the derived members (params/layout/choice_ptrs) from `meta`.
    void finalize();
};

// A live shader node: builds its pipeline from generate_prelude(meta) + meta.body, packs
// its uniform buffer from the layout, and binds 0..2 input textures.
class ShaderFileOp : public OperatorBase, public GpuProcessable {
public:
    explicit ShaderFileOp(std::shared_ptr<const ShaderDef> def);
    ~ShaderFileOp() override;

    void collect_params(std::vector<ParamBase*>& out) override;
    void collect_ports(std::vector<VividPortDescriptor>& out) override;
    void process_gpu(const VividGpuContext* c) override;

    // Non-empty when the shader failed to compile. The node keeps rendering — see the
    // fallback in process_gpu — but the UI surfaces this (S6).
    const std::string& error() const { return error_; }

private:
    bool build(const VividGpuContext* c);            // compile + pipeline; false on failure
    WGPURenderPipeline compile(const VividGpuContext* c, const std::string& body,
                               const char* label, std::string& err);
    void release_pipeline(WGPURenderPipeline& p);

    std::shared_ptr<const ShaderDef> def_;
    std::vector<ParamBase>           params_;        // owned; collect_params hands out pointers
    std::vector<VividPortDescriptor> ports_;
    std::vector<uint8_t>             ubo_staging_;   // CPU-side uniform bytes

    bool               tried_ = false;
    std::string        error_;
    WGPURenderPipeline pipe_     = nullptr;   // the shader's own pipeline (null => failed)
    WGPURenderPipeline fallback_ = nullptr;   // black (generator) / passthrough (filter)
    WGPUBindGroupLayout bgl_ = nullptr;
    WGPUPipelineLayout  pl_  = nullptr;
    WGPUBuffer          ubo_ = nullptr;
    WGPUSampler         samp_ = nullptr;
    WGPUBindGroup       bg_  = nullptr;
};

}  // namespace vivid
