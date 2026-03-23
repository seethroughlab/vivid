#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cctype>
#include <algorithm>

// =============================================================================
// LUT Apply WGSL Shader
// =============================================================================

static const char* kLutApplyFragment = R"(

struct Uniforms {
    resolution: vec2f,
    lut_size: i32,
    intensity: f32,
    interpolation: i32,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;
@group(0) @binding(3) var<storage, read> lut_data: array<vec4f>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

fn lut_index(r: i32, g: i32, b: i32, size: i32) -> i32 {
    return b * size * size + g * size + r;
}

fn lut_fetch(r: i32, g: i32, b: i32, size: i32) -> vec3f {
    let idx = lut_index(
        clamp(r, 0, size - 1),
        clamp(g, 0, size - 1),
        clamp(b, 0, size - 1),
        size
    );
    return lut_data[idx].rgb;
}

fn lut_nearest(rgb: vec3f, size: i32) -> vec3f {
    let scale = f32(size - 1);
    let r = i32(round(rgb.r * scale));
    let g = i32(round(rgb.g * scale));
    let b = i32(round(rgb.b * scale));
    return lut_fetch(r, g, b, size);
}

fn lut_trilinear(rgb: vec3f, size: i32) -> vec3f {
    let scale = f32(size - 1);
    let coords = rgb * scale;

    let r0 = i32(floor(coords.r));
    let g0 = i32(floor(coords.g));
    let b0 = i32(floor(coords.b));

    let fr = fract(coords.r);
    let fg = fract(coords.g);
    let fb = fract(coords.b);

    // 8 corner samples
    let c000 = lut_fetch(r0,     g0,     b0,     size);
    let c100 = lut_fetch(r0 + 1, g0,     b0,     size);
    let c010 = lut_fetch(r0,     g0 + 1, b0,     size);
    let c110 = lut_fetch(r0 + 1, g0 + 1, b0,     size);
    let c001 = lut_fetch(r0,     g0,     b0 + 1, size);
    let c101 = lut_fetch(r0 + 1, g0,     b0 + 1, size);
    let c011 = lut_fetch(r0,     g0 + 1, b0 + 1, size);
    let c111 = lut_fetch(r0 + 1, g0 + 1, b0 + 1, size);

    // Trilinear interpolation
    let c00 = mix(c000, c100, fr);
    let c10 = mix(c010, c110, fr);
    let c01 = mix(c001, c101, fr);
    let c11 = mix(c011, c111, fr);

    let c0 = mix(c00, c10, fg);
    let c1 = mix(c01, c11, fg);

    return mix(c0, c1, fb);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);
    let clamped = clamp(color.rgb, vec3f(0.0), vec3f(1.0));

    var lut_color: vec3f;
    if (uniforms.interpolation == 0) {
        lut_color = lut_nearest(clamped, uniforms.lut_size);
    } else {
        lut_color = lut_trilinear(clamped, uniforms.lut_size);
    }

    let result = mix(color.rgb, lut_color, uniforms.intensity);
    return vec4f(result, color.a);
}
)";

// =============================================================================
// CPU Uniform struct (matches WGSL Uniforms — 32 bytes)
// =============================================================================

struct LutUniforms {
    float resolution[2];   // 8 bytes
    int32_t lut_size;      // 4
    float intensity;       // 4
    int32_t interpolation; // 4
    float _pad0;           // 4
    float _pad1;           // 4
    float _pad2;           // 4
};                         // = 32 bytes

// =============================================================================
// LUT file parsing
// =============================================================================

struct LutData {
    int size = 0;
    std::vector<float> data; // flattened RGB triplets (size^3 * 3)
};

static std::string str_tolower(const std::string& s) {
    std::string out = s;
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

static std::string get_extension(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    return str_tolower(path.substr(dot));
}

static bool parse_cube(const std::string& path, LutData& lut) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    int size = 0;
    float domain_min[3] = {0.f, 0.f, 0.f};
    float domain_max[3] = {1.f, 1.f, 1.f};
    std::vector<float> values;

    std::string line;
    while (std::getline(f, line)) {
        // Strip leading whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line.empty() || line[0] == '#') continue;

        if (line.rfind("TITLE", 0) == 0) continue;

        if (line.rfind("LUT_3D_SIZE", 0) == 0) {
            std::sscanf(line.c_str(), "LUT_3D_SIZE %d", &size);
            continue;
        }
        if (line.rfind("DOMAIN_MIN", 0) == 0) {
            std::sscanf(line.c_str(), "DOMAIN_MIN %f %f %f",
                        &domain_min[0], &domain_min[1], &domain_min[2]);
            continue;
        }
        if (line.rfind("DOMAIN_MAX", 0) == 0) {
            std::sscanf(line.c_str(), "DOMAIN_MAX %f %f %f",
                        &domain_max[0], &domain_max[1], &domain_max[2]);
            continue;
        }

        // Skip other keywords (LUT_1D_SIZE, etc.)
        if (std::isalpha(static_cast<unsigned char>(line[0]))) continue;

        // Data line: three floats
        float r, g, b;
        if (std::sscanf(line.c_str(), "%f %f %f", &r, &g, &b) == 3) {
            // Normalize from domain range to 0..1
            for (int i = 0; i < 3; ++i) {
                float* c = (i == 0) ? &r : (i == 1) ? &g : &b;
                float lo = domain_min[i], hi = domain_max[i];
                if (hi - lo > 1e-6f) {
                    *c = (*c - lo) / (hi - lo);
                }
            }
            values.push_back(r);
            values.push_back(g);
            values.push_back(b);
        }
    }

    if (size < 2) return false;
    int expected = size * size * size * 3;
    if (static_cast<int>(values.size()) != expected) {
        std::fprintf(stderr, "[lut_apply] .cube size mismatch: expected %d values, got %zu\n",
                     expected, values.size());
        return false;
    }

    lut.size = size;
    lut.data = std::move(values);
    return true;
}

static bool parse_3dl(const std::string& path, LutData& lut) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    std::vector<float> values;

    // First line: mesh point indices — skip it
    if (!std::getline(f, line)) return false;

    while (std::getline(f, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        if (line.empty()) continue;

        int r, g, b;
        if (std::sscanf(line.c_str(), "%d %d %d", &r, &g, &b) == 3) {
            // Normalize from 0..4095 range to 0..1
            values.push_back(static_cast<float>(r) / 4095.0f);
            values.push_back(static_cast<float>(g) / 4095.0f);
            values.push_back(static_cast<float>(b) / 4095.0f);
        }
    }

    if (values.empty()) return false;

    // Infer grid size from cube root of entry count
    int entries = static_cast<int>(values.size()) / 3;
    int size = static_cast<int>(std::round(std::cbrt(static_cast<double>(entries))));
    if (size * size * size != entries) {
        std::fprintf(stderr, "[lut_apply] .3dl entry count %d is not a perfect cube\n", entries);
        return false;
    }

    lut.size = size;
    lut.data = std::move(values);
    return true;
}

// =============================================================================
// LUT Apply Operator
// =============================================================================

struct LutApply : vivid::GpuOperatorBase {
    static constexpr const char* kName   = "LUT Apply";
    static constexpr bool kTimeDependent = false;

    vivid::Param<vivid::FilePath> file {"file"};
    vivid::Param<float> intensity      {"intensity", 1.0f, 0.0f, 1.0f};
    vivid::Param<int>   interpolation  {"interpolation", 1, {"Nearest", "Trilinear"}};

    LutApply() {
        vivid::semantic_shape(file, "path");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
        out.push_back(&intensity);
        out.push_back(&interpolation);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (init_failed_) {
            vivid_report_gpu_error(ctx, shader_error_msg_.c_str());
            return;
        }

        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                init_failed_ = true;
                return;
            }
        }

        // Reload LUT when file path changes
        const std::string& path = file.str_value;
        if (path != loaded_path_) {
            loaded_path_ = path;
            load_lut(ctx);
        }

        // Get input texture
        WGPUTextureView input_tex = nullptr;
        if (ctx->input_texture_views && ctx->input_texture_count >= 1)
            input_tex = ctx->input_texture_views[0];
        if (!input_tex && !fallback_view_) create_fallback(ctx);
        if (!input_tex) input_tex = fallback_view_;

        // Update uniforms
        LutUniforms u{};
        u.resolution[0] = static_cast<float>(ctx->output_width);
        u.resolution[1] = static_cast<float>(ctx->output_height);
        u.lut_size       = lut_size_;
        u.intensity      = intensity.value;
        u.interpolation  = interpolation.int_value();
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        // Rebuild bind group when input texture changes
        if (input_tex != cached_input_tex_ || lut_dirty_) {
            rebuild_bind_group(ctx, input_tex);
            cached_input_tex_ = input_tex;
            lut_dirty_ = false;
        }

        if (!cached_bind_group_) return;

        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, cached_bind_group_,
                             ctx->output_texture_view, "LUT Apply");
    }

    ~LutApply() override {
        release_all();
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    WGPURenderPipeline  pipeline_       = nullptr;
    WGPUBindGroupLayout bind_layout_    = nullptr;
    WGPUPipelineLayout  pipe_layout_    = nullptr;
    WGPUShaderModule    shader_         = nullptr;
    WGPUBuffer          uniform_buf_    = nullptr;
    WGPUBuffer          storage_buf_    = nullptr;
    WGPUSampler         sampler_        = nullptr;
    WGPUTexture         fallback_tex_   = nullptr;
    WGPUTextureView     fallback_view_  = nullptr;
    WGPUBindGroup       cached_bind_group_ = nullptr;
    WGPUTextureView     cached_input_tex_  = nullptr;

    std::string loaded_path_;
    int         lut_size_  = 2;  // minimum valid LUT
    bool        lut_dirty_ = true;
    bool        init_failed_ = false;
    std::string shader_error_msg_;

    void release_all() {
        vivid::gpu::release(cached_bind_group_);
        cached_input_tex_ = nullptr;
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(storage_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(sampler_);
    }

    void create_fallback(const VividGpuContext* gpu) {
        WGPUTextureDescriptor td{};
        td.label = vivid_sv("LutApply Fallback");
        td.size = { 1, 1, 1 };
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = gpu->output_format;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        fallback_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

        WGPUTextureViewDescriptor vd{};
        vd.format = gpu->output_format;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        fallback_view_ = wgpuTextureCreateView(fallback_tex_, &vd);

        const uint8_t zero[8] = {};
        WGPUTexelCopyTextureInfo dest_info{};
        dest_info.texture = fallback_tex_;
        dest_info.mipLevel = 0;
        dest_info.origin = {0, 0, 0};
        dest_info.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = 8;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent = {1, 1, 1};
        wgpuQueueWriteTexture(gpu->queue, &dest_info, zero, sizeof(zero), &layout, &extent);
    }

    void load_lut(const VividGpuContext* ctx) {
        LutData lut;
        bool ok = false;

        if (!loaded_path_.empty()) {
            std::string ext = get_extension(loaded_path_);
            if (ext == ".cube") {
                ok = parse_cube(loaded_path_, lut);
            } else if (ext == ".3dl") {
                ok = parse_3dl(loaded_path_, lut);
            } else {
                std::fprintf(stderr, "[lut_apply] Unsupported format: %s\n", ext.c_str());
            }
        }

        if (!ok) {
            // Create identity LUT (2x2x2)
            lut.size = 2;
            lut.data = {
                0,0,0, 1,0,0,
                0,1,0, 1,1,0,
                0,0,1, 1,0,1,
                0,1,1, 1,1,1
            };
            if (!loaded_path_.empty()) {
                std::fprintf(stderr, "[lut_apply] Failed to load: %s — using identity\n",
                             loaded_path_.c_str());
            }
        }

        lut_size_ = lut.size;

        // Convert RGB triplets to vec4 (padded) for GPU storage buffer alignment
        int entries = lut.size * lut.size * lut.size;
        std::vector<float> padded(entries * 4);
        for (int i = 0; i < entries; ++i) {
            padded[i * 4 + 0] = lut.data[i * 3 + 0];
            padded[i * 4 + 1] = lut.data[i * 3 + 1];
            padded[i * 4 + 2] = lut.data[i * 3 + 2];
            padded[i * 4 + 3] = 1.0f;
        }

        // Recreate storage buffer if size changed
        uint64_t buf_size = static_cast<uint64_t>(entries) * 4 * sizeof(float);
        vivid::gpu::release(storage_buf_);
        {
            WGPUBufferDescriptor desc{};
            desc.label = vivid_sv("LutApply LUT Data");
            desc.size = buf_size;
            desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
            storage_buf_ = wgpuDeviceCreateBuffer(ctx->device, &desc);
        }
        wgpuQueueWriteBuffer(ctx->queue, storage_buf_, 0, padded.data(),
                             padded.size() * sizeof(float));

        lut_dirty_ = true;

        if (ok) {
            std::fprintf(stderr, "[lut_apply] Loaded: %s (%dx%dx%d)\n",
                         loaded_path_.c_str(), lut.size, lut.size, lut.size);
        }
    }

    void rebuild_bind_group(const VividGpuContext* ctx, WGPUTextureView input_tex) {
        vivid::gpu::release(cached_bind_group_);

        int entries = lut_size_ * lut_size_ * lut_size_;
        uint64_t storage_size = static_cast<uint64_t>(entries) * 4 * sizeof(float);

        WGPUBindGroupEntry entries_bg[4]{};
        entries_bg[0].binding = 0;
        entries_bg[0].buffer  = uniform_buf_;
        entries_bg[0].offset  = 0;
        entries_bg[0].size    = sizeof(LutUniforms);
        entries_bg[1].binding = 1;
        entries_bg[1].sampler = sampler_;
        entries_bg[2].binding = 2;
        entries_bg[2].textureView = input_tex;
        entries_bg[3].binding = 3;
        entries_bg[3].buffer  = storage_buf_;
        entries_bg[3].offset  = 0;
        entries_bg[3].size    = storage_size;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("LutApply BG");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 4;
        bg_desc.entries = entries_bg;
        cached_bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);
    }

    bool lazy_init(const VividGpuContext* gpu) {
        // Compile shader with error scope
        wgpuDevicePushErrorScope(gpu->device, WGPUErrorFilter_Validation);
        shader_ = vivid::gpu::create_shader(gpu->device, kLutApplyFragment, "LutApply Shader");
        {
            WGPUPopErrorScopeCallbackInfo cb{};
            cb.mode = WGPUCallbackMode_AllowSpontaneous;
            cb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type,
                              WGPUStringView msg, void* ud1, void*) {
                if (type != WGPUErrorType_NoError) {
                    auto* self = static_cast<LutApply*>(ud1);
                    self->shader_error_msg_ = msg.data
                        ? std::string(msg.data, msg.length) : "unknown WGSL error";
                    std::fprintf(stderr, "[lut_apply] WGSL error: %s\n",
                                 self->shader_error_msg_.c_str());
                }
            };
            cb.userdata1 = this;
            wgpuDevicePopErrorScope(gpu->device, cb);
        }
        if (!shader_error_msg_.empty() || !shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(LutUniforms), "LutApply Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "LutApply Sampler");

        // Create initial identity LUT storage buffer
        load_lut(gpu);

        // Bind group layout: uniform(0) + sampler(1) + texture(2) + storage(3)
        WGPUBindGroupLayoutEntry layout_entries[4]{};
        layout_entries[0].binding = 0;
        layout_entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        layout_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        layout_entries[0].buffer.minBindingSize = sizeof(LutUniforms);

        layout_entries[1].binding = 1;
        layout_entries[1].visibility = WGPUShaderStage_Fragment;
        layout_entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        layout_entries[2].binding = 2;
        layout_entries[2].visibility = WGPUShaderStage_Fragment;
        layout_entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        layout_entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
        layout_entries[2].texture.multisampled = false;

        layout_entries[3].binding = 3;
        layout_entries[3].visibility = WGPUShaderStage_Fragment;
        layout_entries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("LutApply BGL");
        bgl_desc.entryCount = 4;
        bgl_desc.entries = layout_entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        // Pipeline layout
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("LutApply Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_,
                                                 gpu->output_format, "LutApply Pipeline");
        return pipeline_ != nullptr;
    }
};

VIVID_REGISTER(LutApply)
