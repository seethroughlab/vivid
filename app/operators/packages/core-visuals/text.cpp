// Core visual package operator: Text — renders a string (read from a .txt file) over
// its input, rasterized with FreeType (hinted glyph atlas). Migrated from the built-in
// TextOp; the AssetShader channel is replaced by a Param<FilePath> (file-param channel,
// since AssetShader's dynamic_cast can't reach a LoadedOperator). Behaviour preserved.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "gpu/freetype_font.h"   // vivid::FtFont (host header; the package links freetype)

#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
const char* kTextWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
struct U { res: vec2f, time: f32, size: f32, pos: vec2f, aspect: f32, pad: f32, color: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var in_tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@group(0) @binding(3) var txt_tex: texture_2d<f32>;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let bg = textureSample(in_tex, samp, inp.uv);
    let scr = u.res.x / max(u.res.y, 1.0);
    let h = u.size;
    let w = u.size * u.aspect / scr;
    let ll = u.pos - vec2f(w, h) * 0.5;
    let local = (inp.uv - ll) / vec2f(w, h);
    var a = 0.0;
    if (local.x >= 0.0 && local.x <= 1.0 && local.y >= 0.0 && local.y <= 1.0) {
        a = textureSample(txt_tex, samp, vec2f(local.x, local.y)).r;
    }
    let cov = a * u.color.a;
    return vec4f(mix(bg.rgb, u.color.rgb, cov), max(bg.a, cov));
}
)";
}  // namespace

struct TextOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Text";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_TRANSFORM;   // ADR-0046
    static constexpr const char* kDisplayName = "Text";
    static constexpr const char* kSummary = "Renders a string (from a .txt file) over its input. Typography.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "text", "typography"};
    vivid::Param<float> size{"size", 0.16f, 0.f, 1.f};
    vivid::Param<float> x{"x", 0.5f, 0.f, 1.f}, y{"y", 0.5f, 0.f, 1.f};
    vivid::Param<float> r{"r", 1.f, 0.f, 1.f}, g{"g", 1.f, 0.f, 1.f}, b{"b", 1.f, 0.f, 1.f}, a{"a", 1.f, 0.f, 1.f};
    vivid::Param<vivid::FilePath> file{"file", ""};   // .txt whose contents are the string

    TextOp() { vivid::asset_kind(file, "text"); }   // ADR-0021/P3: dialog/drop filter

    std::string loaded_path_ = "\x01", text_, baked_text_ = "\x01";
    vivid::FtFont font_; bool font_tried_ = false;
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr;
    WGPUTexture txt_ = nullptr; WGPUTextureView txtv_ = nullptr; WGPUBindGroup bg_ = nullptr;
    int tw_ = 1, th_ = 1; float aspect_ = 1.f;

    ~TextOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (txtv_) wgpuTextureViewRelease(txtv_); if (txt_) wgpuTextureRelease(txt_);
        if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&size); o.push_back(&x); o.push_back(&y);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&a);
        o.push_back(&file);   // last: keeps float-param indices 0..6 stable
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    void ensure_font() {
        if (font_.ok || font_tried_) return;
        font_tried_ = true;
        font_.load(VIVID_FONT_PATH, 180);
    }
    void bake(const VividGpuContext* c) {
        ensure_font();
        const std::string s = (text_.empty() || !font_.ok) ? std::string() : text_;
        FT_Face face = font_.face;
        const int base = font_.ascent() + 1;
        const int H = std::max(1, font_.ascent() + font_.descent() + 2);
        float wf = 0.f; uint32_t prev = 0;
        for (unsigned char ch : s) {
            wf += font_.kerning(prev, ch);
            if (!FT_Load_Char(face, ch, FT_LOAD_RENDER)) wf += static_cast<float>(face->glyph->advance.x >> 6);
            prev = ch;
        }
        const int W = std::max(1, static_cast<int>(wf) + 2);
        std::vector<unsigned char> bmp(static_cast<size_t>(W) * H, 0);
        float pen = 1.f; prev = 0;
        for (unsigned char ch : s) {
            pen += font_.kerning(prev, ch);
            if (FT_Load_Char(face, ch, FT_LOAD_RENDER)) { prev = ch; continue; }
            FT_GlyphSlot gsl = face->glyph;
            const int ox = static_cast<int>(pen) + gsl->bitmap_left, oy = base - gsl->bitmap_top;
            for (int row = 0; row < static_cast<int>(gsl->bitmap.rows); ++row)
                for (int col = 0; col < static_cast<int>(gsl->bitmap.width); ++col) {
                    const int ax = ox + col, ay = oy + row;
                    if (ax >= 0 && ay >= 0 && ax < W && ay < H)
                        bmp[static_cast<size_t>(ay) * W + ax] = gsl->bitmap.buffer[row * gsl->bitmap.pitch + col];
                }
            pen += static_cast<float>(gsl->advance.x >> 6);
            prev = ch;
        }
        if (txtv_) { wgpuTextureViewRelease(txtv_); txtv_ = nullptr; }
        if (txt_)  { wgpuTextureRelease(txt_); txt_ = nullptr; }
        WGPUTextureDescriptor td{};
        td.size = { static_cast<uint32_t>(W), static_cast<uint32_t>(H), 1 };
        td.mipLevelCount = 1; td.sampleCount = 1; td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_R8Unorm;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        txt_ = wgpuDeviceCreateTexture(c->device, &td);
        WGPUTextureViewDescriptor vd{};
        vd.format = WGPUTextureFormat_R8Unorm; vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
        txtv_ = wgpuTextureCreateView(txt_, &vd);
        const uint32_t stride = ((static_cast<uint32_t>(W) + 255u) / 256u) * 256u;
        std::vector<unsigned char> padded(static_cast<size_t>(stride) * H, 0);
        for (int row = 0; row < H; ++row)
            std::memcpy(padded.data() + static_cast<size_t>(row) * stride, bmp.data() + static_cast<size_t>(row) * W, W);
        WGPUTexelCopyTextureInfo dst{}; dst.texture = txt_; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay{}; lay.bytesPerRow = stride; lay.rowsPerImage = static_cast<uint32_t>(H);
        WGPUExtent3D ext = { static_cast<uint32_t>(W), static_cast<uint32_t>(H), 1 };
        wgpuQueueWriteTexture(c->queue, &dst, padded.data(), padded.size(), &lay, &ext);
        tw_ = W; th_ = H; aspect_ = static_cast<float>(W) / static_cast<float>(H);
        baked_text_ = text_;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kTextWGSL, "Text", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 48, "Text U");
        WGPUBindGroupLayoutEntry e[4]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 48;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment; e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        e[3].binding = 3; e[3].visibility = WGPUShaderStage_Fragment;
        e[3].texture.sampleType = WGPUTextureSampleType_Float; e[3].texture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 4; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Text Pipeline");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        if (file.str_value != loaded_path_) {   // reload the string (the .txt file's contents) on change
            loaded_path_ = file.str_value;
            text_.clear();
            if (!file.str_value.empty()) {
                std::ifstream f(file.str_value);
                if (f) text_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
                while (!text_.empty() && (text_.back() == '\n' || text_.back() == '\r' || text_.back() == ' ')) text_.pop_back();
            }
        }
        if (!txt_ || text_ != baked_text_) bake(c);
        const float* p = c->param_values;   // size,x,y,r,g,b,a (file is param 7 -> file_param_values)
        auto pv = [&](int i, float d) { return p ? p[i] : d; };
        float u[12] = {
            float(c->output_width), float(c->output_height), float(c->time), pv(0, size.value),
            pv(1, x.value), pv(2, y.value), aspect_, 0.f,
            pv(3, r.value), pv(4, g.value), pv(5, b.value), pv(6, a.value),
        };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        const WGPUTextureView in = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[4]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = 48;
        be[1].binding = 1; be[1].textureView = in;
        be[2].binding = 2; be[2].sampler = samp_;
        be[3].binding = 3; be[3].textureView = txtv_;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 4; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Text");
    }
};

VIVID_REGISTER(TextOp)

// ADR-0021/P3: drop a .txt onto the graph -> a Text node reading it.
static const char* const kTextDropExts[] = { ".txt" };
static const VividFileDropHandlerDescriptor kTextDrop[] = {
    { "Text", kTextDropExts, 1, "file", 5, "Render as text" }
};
VIVID_FILE_DROP(kTextDrop)
