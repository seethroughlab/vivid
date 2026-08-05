#pragma once

// ADR-0016 / S3+S4 — the operator a SHADER FILE becomes.
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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <webgpu/webgpu.h>

namespace vivid {

// The parsed definition of ONE shader file.
//
// It must outlive every instance AND every cached descriptor: `ParamBase::name`, `group`,
// `description` and `choice_labels` are raw `const char*`, and the registry builds its
// per-type descriptor from a TEMPORARY instance. Pointing those at strings owned by the
// instance would leave the cached descriptor dangling the moment that temp died. They point
// in here instead, and the library keeps this alive for the whole run.
struct ShaderDef {
    ShaderMeta                   meta;
    std::vector<ShaderHostParam> params;      // the declared params, expanded (color -> r/g/b, ...)
    UniformLayout                layout;
    std::string                  path;        // the file on disk (hot-reload, fork-to-edit)
    std::string                  tier;        // "user" | "project" | "bundled"

    // Stable const char* arrays for the enum params' choice labels.
    std::vector<std::vector<const char*>> choice_ptrs;   // parallel to params

    void finalize();   // build params/layout/choice_ptrs from `meta`

    // Do these two declare the same operator INTERFACE (ports + params)? A body edit hot-reloads
    // in place; an interface change needs the node instances rebuilt.
    bool same_interface(const ShaderDef& other) const;
};

// The mutable cell a shader file's definition lives in. The registry factory and every live
// instance hold the SLOT, not the def — so a reload swaps `def` and bumps `generation`, and
// each node picks the new version up on its next frame. Main thread only.
struct ShaderSlot {
    std::shared_ptr<const ShaderDef> def;
    uint64_t generation = 0;
};

// A live shader node: builds its pipeline from generate_prelude(meta) + meta.body, packs its
// uniform buffer from the layout, and binds 0..2 input textures.
class ShaderFileOp : public OperatorBase, public GpuProcessable {
public:
    explicit ShaderFileOp(std::shared_ptr<ShaderSlot> slot);
    ~ShaderFileOp() override;

    void collect_params(std::vector<ParamBase*>& out) override;
    void collect_ports(std::vector<VividPortDescriptor>& out) override;
    void process_gpu(const VividGpuContext* c) override;

    // ADR-0046: the role is declared in the shader header; forward it so a shader op is classified in the
    // catalog/chooser exactly like a compiled op. def_ is bound at construction, so this is valid on the
    // temporary instance the registry builds its cached descriptor from.
    VividOperatorRole declared_operator_role() const override { return def_->meta.role; }

    // Non-empty when the shader failed to compile. The node keeps rendering — see the
    // fallback in process_gpu — but the UI surfaces this.
    const std::string& error() const { return error_; }

private:
    bool build(const VividGpuContext* c);            // one-time: bindings, buffer, sampler, pipeline
    bool rebuild_pipeline(const VividGpuContext* c); // a body edit: recompile, keep last-good on failure
    WGPURenderPipeline compile(const VividGpuContext* c, const std::string& body,
                               const char* label, std::string& err);
    static void release_pipeline(WGPURenderPipeline& p);

    std::shared_ptr<ShaderSlot>      slot_;
    std::shared_ptr<const ShaderDef> def_;           // the version THIS instance was built against
    uint64_t                         gen_ = 0;
    std::vector<ParamBase>           params_;        // owned; collect_params hands out pointers
    std::vector<VividPortDescriptor> ports_;
    std::vector<uint8_t>             ubo_staging_;   // CPU-side uniform bytes

    bool               tried_ = false;
    std::string        error_;
    WGPURenderPipeline pipe_     = nullptr;   // the shader's own pipeline (null => never compiled)
    WGPURenderPipeline fallback_ = nullptr;   // black (source) / passthrough (filter)
    WGPUBindGroupLayout bgl_ = nullptr;
    WGPUPipelineLayout  pl_  = nullptr;
    WGPUBuffer          ubo_ = nullptr;
    WGPUSampler         samp_ = nullptr;
    WGPUBindGroup       bg_  = nullptr;
};

}  // namespace vivid
