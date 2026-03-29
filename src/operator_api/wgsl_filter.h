#pragma once

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/wgsl_preprocessor.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <sys/stat.h>

namespace vivid {

// WGSL reserved keywords (W3C spec §14.3).  Param names that collide with
// these will fail naga parsing with an opaque error, so we reject them early.
inline bool is_wgsl_reserved(const std::string& name) {
    static const std::unordered_set<std::string> reserved = {
        "NULL", "Self", "abstract", "active", "alignas", "alignof", "as",
        "asm", "asm_fragment", "async", "attribute", "auto", "await",
        "become", "binding_array", "cast", "catch", "class", "co_await",
        "co_return", "co_yield", "coherent", "column_major", "common",
        "compile", "compile_fragment", "concept", "const_cast", "consteval",
        "constexpr", "constinit", "crate", "debugger", "decltype", "delete",
        "demote", "demote_to_helper", "do", "dynamic_cast", "enum",
        "explicit", "export", "extends", "extern", "external", "fallthrough",
        "filter", "final", "finally", "friend", "from", "fxgroup", "get",
        "goto", "groupshared", "highp", "impl", "implements", "import",
        "inline", "instanceof", "interface", "layout", "lowp", "macro",
        "macro_rules", "match", "mediump", "meta", "mod", "module", "move",
        "mut", "mutable", "namespace", "new", "nil", "noexcept",
        "noinline", "nointerpolation", "noperspective", "null", "nullptr",
        "of", "operator", "package", "packoffset", "partition", "pass",
        "patch", "pixelfragment", "precise", "precision", "premerge",
        "priv", "protected", "pub", "public", "readonly", "ref", "regard",
        "register", "reinterpret_cast", "require", "resource",
        "restrict", "self", "set", "shared", "sizeof", "smooth", "snorm",
        "static", "static_assert", "static_cast", "std", "subroutine",
        "super", "target", "template", "this", "thread_local", "throw",
        "trait", "try", "type", "typedef", "typeid", "typename",
        "typeof", "union", "unless", "unorm", "unsafe", "unsized",
        "use", "using", "varying", "virtual", "volatile", "wgsl",
        "where", "with", "writeonly", "yield",
    };
    return reserved.count(name) > 0;
}

// =============================================================================
// WgslFilterBase — generic base class for WGSL fragment-shader filters.
//
// Subclasses declare params and point to a .wgsl file containing only
// @fragment fn fs_main(...).  Everything else (pipeline, uniforms, vertex
// shader, texture bindings, hot-reload) is handled here.
// =============================================================================

struct WgslFilterBase : OperatorBase, GpuProcessable {

    explicit WgslFilterBase(const char* shader_filename)
        : shader_filename_(shader_filename) {}

protected:
    // Override the shader path entirely (for data-driven filters).
    // Must be called before the first process_gpu() frame.
    void set_shader_path_override(const std::string& path) {
        shader_path_override_ = path;
    }

public:
    // --- GpuProcessable overrides -------------------------------------------

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!initialized_) {
            if (!lazy_init(ctx)) {
                if (shader_error_) vivid_report_gpu_error(ctx, shader_error_msg_.c_str());
                return;
            }
        }

        if (has_shader_error()) {
            vivid_report_gpu_error(ctx, shader_error_msg_.c_str());
            return;
        }

        // Shader hot-reload: check file every 30 frames
        reload_counter_++;
        if (reload_counter_ >= 30) {
            reload_counter_ = 0;
            check_hot_reload(ctx);
        }

        // Update uniform buffer
        fill_uniforms(ctx);
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_.get(), 0,
                             uniform_data_.data(), uniform_size_);

        // Resolve N input textures, using fallback for disconnected
        std::vector<WGPUTextureView> input_texs(tex_input_count_);
        for (uint32_t i = 0; i < tex_input_count_; ++i) {
            WGPUTextureView v = nullptr;
            if (ctx->input_texture_views && i < ctx->input_texture_count)
                v = ctx->input_texture_views[i];
            if (!v) {
                if (!fallback_view_) create_fallback(ctx);
                v = fallback_view_.get();
            }
            input_texs[i] = v;
        }

        // Recreate bind group if any input texture changed
        if (input_texs != cached_input_texs_) {
            bind_group_.reset(create_bind_group(ctx, input_texs));
            cached_input_texs_ = input_texs;
        }

        vivid::gpu::run_pass(ctx->command_encoder, pipeline_.get(), bind_group_.get(),
                             ctx->output_texture_view, "WgslFilter Pass");
    }

    ~WgslFilterBase() override = default;

    bool has_shader_error() const { return shader_error_; }
    const std::string& shader_error_msg() const { return shader_error_msg_; }

private:
    std::string shader_filename_;
    std::string shader_path_override_;

    // GPU resources (RAII handles)
    gpu::PipelineHandle   pipeline_;
    gpu::BindLayoutHandle bind_layout_;
    gpu::BufferHandle     uniform_buf_;
    gpu::ShaderHandle     shader_;
    gpu::PipeLayoutHandle pipe_layout_;
    gpu::SamplerHandle    sampler_;
    gpu::BindGroupHandle  bind_group_;
    gpu::TextureHandle    fallback_tex_;
    gpu::TexViewHandle    fallback_view_;

    uint32_t tex_input_count_ = 0;
    std::vector<WGPUTextureView> cached_input_texs_;

    // Uniform buffer
    std::vector<uint8_t> uniform_data_;
    uint32_t uniform_size_ = 0;
    uint32_t param_count_  = 0;

    // Hot-reload state
    std::string shader_path_;
    time_t last_mtime_ = 0;
    uint32_t reload_counter_ = 0;
    bool initialized_ = false;

    // Shader error state (deferred: UI surfacing not yet implemented)
    bool shader_error_ = false;
    std::string shader_error_msg_;

    // Cached GPU state for hot-reload
    WGPUDevice cached_device_ = nullptr;
    WGPUTextureFormat cached_format_ = WGPUTextureFormat_Undefined;

    // -----------------------------------------------------------------------
    // Preamble generation — builds WGSL prefix from param names
    // -----------------------------------------------------------------------
    std::string generate_preamble() {
        std::vector<ParamBase*> params;
        collect_params(params);
        param_count_ = static_cast<uint32_t>(params.size());

        std::ostringstream s;

        // Fullscreen triangle helper + math constants
        s << vivid::gpu::FULLSCREEN_VERTEX_WGSL << "\n";
        s << vivid::gpu::WGSL_CONSTANTS << "\n";

        // Uniforms struct
        s << "struct Uniforms {\n";
        s << "    resolution: vec2f,\n";
        s << "    time: f32,\n";
        s << "    frame: u32,\n";
        for (uint32_t i = 0; i < param_count_; ++i) {
            if (is_wgsl_reserved(std::string(params[i]->name))) {
                std::fprintf(stderr, "[wgsl_filter] Param '%s' is a WGSL reserved keyword"
                             " — rename it to avoid shader compilation failure.  shader: %s\n",
                             params[i]->name, shader_filename_.c_str());
            }
            s << "    " << params[i]->name << ": f32,\n";
        }
        s << "}\n\n";

        // VertexOutput
        s << "struct VertexOutput {\n";
        s << "    @builtin(position) position: vec4f,\n";
        s << "    @location(0) uv: vec2f,\n";
        s << "}\n\n";

        // Bindings
        s << "@group(0) @binding(0) var<uniform> u: Uniforms;\n";
        s << "@group(0) @binding(1) var texSampler: sampler;\n";
        if (tex_input_count_ <= 1) {
            s << "@group(0) @binding(2) var inputTex: texture_2d<f32>;\n\n";
        } else {
            for (uint32_t i = 0; i < tex_input_count_; ++i)
                s << "@group(0) @binding(" << (2 + i) << ") var inputTex"
                  << i << ": texture_2d<f32>;\n";
            s << "\n";
        }

        // Vertex shader
        s << "@vertex\n";
        s << "fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {\n";
        s << "    let fs = fullscreenTriangle(vertexIndex, true);\n";
        s << "    var out: VertexOutput;\n";
        s << "    out.position = fs.position;\n";
        s << "    out.uv = fs.uv;\n";
        s << "    return out;\n";
        s << "}\n\n";

        return s.str();
    }

    // -----------------------------------------------------------------------
    // Uniform buffer helpers
    // -----------------------------------------------------------------------
    void compute_uniform_size() {
        uint32_t raw = 16 + 4 * param_count_;  // header (16) + params
        uniform_size_ = (raw + 15) & ~15u;     // round up to 16-byte alignment
        uniform_data_.resize(uniform_size_, 0);
    }

    void fill_uniforms(const VividGpuContext* ctx) {
        std::memset(uniform_data_.data(), 0, uniform_size_);
        float* f = reinterpret_cast<float*>(uniform_data_.data());
        f[0] = static_cast<float>(ctx->output_width);
        f[1] = static_cast<float>(ctx->output_height);
        f[2] = static_cast<float>(ctx->time);
        uint32_t* u = reinterpret_cast<uint32_t*>(uniform_data_.data());
        u[3] = static_cast<uint32_t>(ctx->frame);
        for (uint32_t i = 0; i < param_count_; ++i) {
            f[4 + i] = ctx->param_values[i];
        }
    }

    // -----------------------------------------------------------------------
    // File I/O
    // -----------------------------------------------------------------------
    static std::string read_file(const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs) return {};
        std::ostringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

    static time_t file_mtime(const std::string& path) {
        struct stat st{};
        if (stat(path.c_str(), &st) != 0) return 0;
        return st.st_mtime;
    }

    // -----------------------------------------------------------------------
    // Resolve shader path from operators_src_dir + shader_filename
    // -----------------------------------------------------------------------
    bool resolve_shader_path(const VividGpuContext* ctx) {
        // Data-driven filters supply their own path
        if (!shader_path_override_.empty()) {
            shader_path_ = shader_path_override_;
            return true;
        }

        if (!ctx->operators_src_dir) return false;

        // Derive stem from filename: "posterize.wgsl" → "posterize"
        std::string fname = shader_filename_;
        std::string stem = fname;
        auto dot = stem.rfind('.');
        if (dot != std::string::npos) stem = stem.substr(0, dot);

        shader_path_ = std::string(ctx->operators_src_dir) +
                        "/gpu/" + stem + "/" + fname;
        return true;
    }

    // -----------------------------------------------------------------------
    // Shader compilation — returns nullptr on failure
    // -----------------------------------------------------------------------
    WGPUShaderModule compile_shader(WGPUDevice device, const std::string& preamble,
                                    const std::string& fragment_src) {
        std::string full = preamble + "\n" + fragment_src;

        WGPUShaderSourceWGSL wgsl_src{};
        wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl_src.code = vivid_sv(full.c_str());

        WGPUShaderModuleDescriptor desc{};
        desc.nextInChain = &wgsl_src.chain;
        desc.label = vivid_sv("WgslFilter Shader");

        // Wrap in error scope so naga parsing/validation errors are captured
        // immediately — otherwise wgpu returns an "error object" (non-null but
        // invalid) and the real message only surfaces at pipeline creation as
        // the opaque "ShaderModule is invalid".
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
        WGPUShaderModule mod = wgpuDeviceCreateShaderModule(device, &desc);
        struct ErrData { bool has_error = false; std::string msg; };
        ErrData err;
        WGPUPopErrorScopeCallbackInfo pop_cb{};
        pop_cb.mode = WGPUCallbackMode_AllowSpontaneous;
        pop_cb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type,
                              WGPUStringView msg, void* ud1, void*) {
            if (type != WGPUErrorType_NoError) {
                auto* e = static_cast<ErrData*>(ud1);
                e->has_error = true;
                e->msg = msg.data ? std::string(msg.data, msg.length) : "unknown error";
            }
        };
        pop_cb.userdata1 = &err;
        wgpuDevicePopErrorScope(device, pop_cb);
        if (err.has_error) {
            std::fprintf(stderr, "[wgsl_filter] Shader compilation error: %s\n  shader: %s\n",
                         err.msg.c_str(), shader_path_.c_str());
        }
        return mod;
    }

    // -----------------------------------------------------------------------
    // Pipeline creation
    // -----------------------------------------------------------------------
    WGPURenderPipeline create_pipeline(WGPUDevice device, WGPUShaderModule shader,
                                       WGPUTextureFormat format) {
        return vivid::gpu::create_pipeline(device, shader, pipe_layout_.get(), format, "WgslFilter Pipeline");
    }

    // -----------------------------------------------------------------------
    // Bind group creation (recreated when input texture changes)
    // -----------------------------------------------------------------------
    WGPUBindGroup create_bind_group(const VividGpuContext* ctx,
                                    const std::vector<WGPUTextureView>& textures) {
        std::vector<WGPUBindGroupEntry> entries(2 + textures.size(), WGPUBindGroupEntry{});
        entries[0].binding = 0;
        entries[0].buffer  = uniform_buf_.get();
        entries[0].offset  = 0;
        entries[0].size    = uniform_size_;
        entries[1].binding = 1;
        entries[1].sampler = sampler_.get();
        for (size_t i = 0; i < textures.size(); ++i) {
            entries[2 + i].binding = static_cast<uint32_t>(2 + i);
            entries[2 + i].textureView = textures[i];
        }

        WGPUBindGroupDescriptor desc{};
        desc.label = vivid_sv("WgslFilter BG");
        desc.layout = bind_layout_.get();
        desc.entryCount = static_cast<uint32_t>(entries.size());
        desc.entries = entries.data();
        return wgpuDeviceCreateBindGroup(ctx->device, &desc);
    }

    // -----------------------------------------------------------------------
    // Fallback 1x1 texture for disconnected inputs
    // -----------------------------------------------------------------------
    void create_fallback(const VividGpuContext* ctx) {
        WGPUTextureDescriptor td{};
        td.label = vivid_sv("WgslFilter Fallback");
        td.size = { 1, 1, 1 };
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = ctx->output_format;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        fallback_tex_.reset(wgpuDeviceCreateTexture(ctx->device, &td));

        WGPUTextureViewDescriptor vd{};
        vd.format = ctx->output_format;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        fallback_view_.reset(wgpuTextureCreateView(fallback_tex_.get(), &vd));

        const uint8_t zero[8] = {};  // transparent black (up to 8 bytes for RGBA16Float)
        WGPUTexelCopyTextureInfo dest{};
        dest.texture = fallback_tex_.get();
        dest.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = 8;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent = { 1, 1, 1 };
        wgpuQueueWriteTexture(ctx->queue, &dest, zero, sizeof(zero), &layout, &extent);
    }

    // -----------------------------------------------------------------------
    // lazy_init — first-frame setup
    // -----------------------------------------------------------------------
    bool lazy_init(const VividGpuContext* ctx) {
        // Count GPU_TEXTURE input ports
        std::vector<VividPortDescriptor> ports;
        collect_ports(ports);
        tex_input_count_ = 0;
        for (auto& p : ports)
            if (p.direction == VIVID_PORT_INPUT && p.type == VIVID_PORT_TEXTURE)
                tex_input_count_++;
        cached_input_texs_.resize(tex_input_count_, nullptr);

        // Generate preamble (also counts params)
        std::string preamble = generate_preamble();
        compute_uniform_size();

        // Resolve shader file path
        if (!resolve_shader_path(ctx)) {
            std::fprintf(stderr, "[wgsl_filter] No operators_src_dir, cannot locate %s\n",
                         shader_filename_.c_str());
            return false;
        }

        // Read + preprocess fragment shader (supports // @include with diagnostics)
        auto pp = preprocess_wgsl_file(shader_path_);
        if (!pp.ok) {
            shader_error_ = true;
            shader_error_msg_ = pp.error;
            std::fprintf(stderr, "[wgsl_filter] Preprocess error: %s\n", pp.error.c_str());
            return false;
        }
        std::string fragment_src = std::move(pp.output);
        last_mtime_ = file_mtime(shader_path_);

        // Compile shader
        WGPUShaderModule sm = compile_shader(ctx->device, preamble, fragment_src);
        if (!sm) {
            std::fprintf(stderr, "[wgsl_filter] Shader compile failed: %s\n", shader_path_.c_str());
            return false;
        }
        shader_.reset(sm);

        uniform_buf_.reset(vivid::gpu::create_uniform_buffer(ctx->device, uniform_size_, "WgslFilter Uniforms"));
        sampler_.reset(vivid::gpu::create_linear_sampler(ctx->device, "WgslFilter Sampler"));

        // Bind group layout: uniform(0) + sampler(1) + N textures(2..)
        std::vector<WGPUBindGroupLayoutEntry> bgl_entries(2 + tex_input_count_, WGPUBindGroupLayoutEntry{});
        bgl_entries[0].binding = 0;
        bgl_entries[0].visibility = WGPUShaderStage_Fragment;
        bgl_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entries[0].buffer.minBindingSize = uniform_size_;

        bgl_entries[1].binding = 1;
        bgl_entries[1].visibility = WGPUShaderStage_Fragment;
        bgl_entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        for (uint32_t i = 0; i < tex_input_count_; ++i) {
            bgl_entries[2 + i].binding = 2 + i;
            bgl_entries[2 + i].visibility = WGPUShaderStage_Fragment;
            bgl_entries[2 + i].texture.sampleType = WGPUTextureSampleType_Float;
            bgl_entries[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
            bgl_entries[2 + i].texture.multisampled = false;
        }

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("WgslFilter BGL");
        bgl_desc.entryCount = static_cast<uint32_t>(bgl_entries.size());
        bgl_desc.entries = bgl_entries.data();
        bind_layout_.reset(wgpuDeviceCreateBindGroupLayout(ctx->device, &bgl_desc));

        // Pipeline layout
        WGPUBindGroupLayout raw_layout = bind_layout_.get();
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("WgslFilter Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &raw_layout;
        pipe_layout_.reset(wgpuDeviceCreatePipelineLayout(ctx->device, &pl_desc));

        // Render pipeline — use error scope to detect validation failures that
        // wgpu-native reports as "error pipeline" objects (non-null but invalid).
        wgpuDevicePushErrorScope(ctx->device, WGPUErrorFilter_Validation);
        WGPURenderPipeline rp = create_pipeline(ctx->device, shader_.get(), ctx->output_format);
        {
            struct ErrData { bool has_error = false; std::string msg; };
            ErrData err;
            WGPUPopErrorScopeCallbackInfo pop_cb{};
            pop_cb.mode = WGPUCallbackMode_AllowSpontaneous;
            pop_cb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type,
                                  WGPUStringView msg, void* ud1, void*) {
                if (type != WGPUErrorType_NoError) {
                    auto* e = static_cast<ErrData*>(ud1);
                    e->has_error = true;
                    e->msg = msg.data ? std::string(msg.data, msg.length) : "unknown error";
                }
            };
            pop_cb.userdata1 = &err;
            wgpuDevicePopErrorScope(ctx->device, pop_cb);
            if (err.has_error) {
                std::fprintf(stderr, "[wgsl_filter] Pipeline validation error: %s\n  shader: %s\n",
                             err.msg.c_str(), shader_path_.c_str());
                shader_error_ = true;
                shader_error_msg_ = err.msg;
                return false;
            }
        }
        if (!rp) {
            std::fprintf(stderr, "[wgsl_filter] Pipeline creation failed: %s\n", shader_path_.c_str());
            return false;
        }
        pipeline_.reset(rp);

        // Generators have 0 input textures, so the "changed" check in
        // process_gpu() would never trigger — create the bind group now.
        if (tex_input_count_ == 0)
            bind_group_.reset(create_bind_group(ctx, cached_input_texs_));

        cached_device_ = ctx->device;
        cached_format_ = ctx->output_format;
        initialized_ = true;

        std::fprintf(stderr, "[wgsl_filter] Initialized: %s (%u params, %u byte uniforms)\n",
                     shader_path_.c_str(), param_count_, uniform_size_);
        return true;
    }

    // -----------------------------------------------------------------------
    // Hot-reload — check mtime and recompile on change
    // -----------------------------------------------------------------------
    void check_hot_reload(const VividGpuContext* ctx) {
        if (shader_path_.empty()) return;

        time_t mt = file_mtime(shader_path_);
        if (mt == 0 || mt == last_mtime_) return;

        std::fprintf(stderr, "[wgsl_filter] Shader changed, recompiling: %s\n", shader_path_.c_str());

        auto pp = preprocess_wgsl_file(shader_path_);
        if (!pp.ok) {
            shader_error_ = true;
            shader_error_msg_ = pp.error;
            std::fprintf(stderr, "[wgsl_filter] Preprocess error (keeping old): %s\n",
                         pp.error.c_str());
            last_mtime_ = mt;  // don't retry every 30 frames
            return;
        }
        std::string fragment_src = std::move(pp.output);

        std::string preamble = generate_preamble();
        WGPUShaderModule sm = compile_shader(ctx->device, preamble, fragment_src);
        if (!sm) {
            shader_error_ = true;
            shader_error_msg_ = "Compile error: " + shader_path_;
            std::fprintf(stderr, "[wgsl_filter] Compile error (keeping old pipeline): %s\n",
                         shader_path_.c_str());
            last_mtime_ = mt;  // don't retry every 30 frames
            return;
        }

        WGPURenderPipeline rp = create_pipeline(ctx->device, sm, ctx->output_format);
        if (!rp) {
            wgpuShaderModuleRelease(sm);
            shader_error_ = true;
            shader_error_msg_ = "Pipeline error: " + shader_path_;
            std::fprintf(stderr, "[wgsl_filter] Pipeline error (keeping old): %s\n",
                         shader_path_.c_str());
            last_mtime_ = mt;
            return;
        }

        // Success — swap in new shader + pipeline, clear error
        shader_error_ = false;
        shader_error_msg_.clear();
        shader_.reset(sm);
        pipeline_.reset(rp);
        // Force bind group recreation on next frame
        cached_input_texs_.assign(tex_input_count_, nullptr);
        last_mtime_ = mt;

        std::fprintf(stderr, "[wgsl_filter] Hot-reloaded: %s\n", shader_path_.c_str());
    }
};

} // namespace vivid
