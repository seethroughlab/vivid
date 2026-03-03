#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

// =============================================================================
// MeshImport Operator — loads OBJ and glTF/GLB files with PBR materials
// =============================================================================

struct MeshImport : vivid::OperatorBase {
    static constexpr const char* kName   = "MeshImport";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<vivid::FilePath> file {"file"};

    // Color
    vivid::Param<float> r {"r", 0.8f, 0.0f, 1.0f};
    vivid::Param<float> g {"g", 0.8f, 0.0f, 1.0f};
    vivid::Param<float> b {"b", 0.8f, 0.0f, 1.0f};
    vivid::Param<float> a {"a", 1.0f, 0.0f, 1.0f};

    // Transform
    vivid::Param<float> pos_x   {"pos_x",   0.0f, -50.0f, 50.0f};
    vivid::Param<float> pos_y   {"pos_y",   0.0f, -50.0f, 50.0f};
    vivid::Param<float> pos_z   {"pos_z",   0.0f, -50.0f, 50.0f};
    vivid::Param<float> rot_x   {"rot_x",   0.0f, -6.283f, 6.283f};
    vivid::Param<float> rot_y   {"rot_y",   0.0f, -6.283f, 6.283f};
    vivid::Param<float> rot_z   {"rot_z",   0.0f, -6.283f, 6.283f};
    vivid::Param<float> scale_x {"scale_x", 1.0f,  0.01f, 50.0f};
    vivid::Param<float> scale_y {"scale_y", 1.0f,  0.01f, 50.0f};
    vivid::Param<float> scale_z {"scale_z", 1.0f,  0.01f, 50.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(file, "File");

        vivid::param_group(r, "Color");
        vivid::param_group(g, "Color");
        vivid::param_group(b, "Color");
        vivid::param_group(a, "Color");
        vivid::display_hint(r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(b, VIVID_DISPLAY_COLOR);

        vivid::param_group(pos_x, "Transform");
        vivid::param_group(pos_y, "Transform");
        vivid::param_group(pos_z, "Transform");
        vivid::param_group(rot_x, "Transform");
        vivid::param_group(rot_y, "Transform");
        vivid::param_group(rot_z, "Transform");
        vivid::param_group(scale_x, "Transform");
        vivid::param_group(scale_y, "Transform");
        vivid::param_group(scale_z, "Transform");

        out.push_back(&file);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&a);
        out.push_back(&pos_x);
        out.push_back(&pos_y);
        out.push_back(&pos_z);
        out.push_back(&rot_x);
        out.push_back(&rot_y);
        out.push_back(&rot_z);
        out.push_back(&scale_x);
        out.push_back(&scale_y);
        out.push_back(&scale_z);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"scene", VIVID_PORT_GPU_SCENE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        // Reload if file path changed
        if (file.str_value != cached_path_) {
            cached_path_ = file.str_value;
            release_textures();
            load_mesh(gpu);
        }

        if (!vertex_buffer_ || !index_buffer_) return;

        // Lazy-init GPU textures on first process() after load
        if (has_gltf_material_ && !textures_inited_)
            lazy_init_textures(gpu);

        // Build model matrix: T * Rz * Ry * Rx * S
        float sx = scale_x.value, sy = scale_y.value, sz = scale_z.value;
        float rx = rot_x.value,   ry = rot_y.value,   rz = rot_z.value;
        float px = pos_x.value,   py = pos_y.value,   pz = pos_z.value;

        mat4x4 S, tmp;
        mat4x4_identity(S);
        mat4x4_scale_aniso(S, S, sx, sy, sz);
        mat4x4_rotate_X(tmp, S, rx);
        mat4x4_rotate_Y(S, tmp, ry);
        mat4x4_rotate_Z(tmp, S, rz);

        mat4x4 T;
        mat4x4_translate(T, px, py, pz);
        mat4x4_mul(fragment_.model_matrix, T, tmp);

        fragment_.vertex_buffer   = vertex_buffer_;
        fragment_.vertex_buf_size = vertex_buf_size_;
        fragment_.index_buffer    = index_buffer_;
        fragment_.index_count     = index_count_;
        fragment_.cpu_vertices     = cpu_verts_.data();
        fragment_.cpu_vertex_count = static_cast<uint32_t>(cpu_verts_.size());
        fragment_.cpu_indices      = cpu_indices_.data();
        fragment_.cpu_index_count  = static_cast<uint32_t>(cpu_indices_.size());

        fragment_.pipeline       = nullptr;
        fragment_.material_binds = nullptr;

        if (has_gltf_material_ && textures_inited_) {
            // Apply glTF PBR material
            fragment_.color[0] = gltf_base_color_[0];
            fragment_.color[1] = gltf_base_color_[1];
            fragment_.color[2] = gltf_base_color_[2];
            fragment_.color[3] = gltf_base_color_[3];
            fragment_.roughness = gltf_roughness_;
            // Shader uses additive metallic: material.metallic + rm_tex.g
            // When R/M texture is present, metallic factor is baked into the texture
            // during swizzle, so scalar must be 0 to avoid double-counting.
            fragment_.metallic  = has_rm_texture_ ? 0.0f : gltf_metallic_;
            fragment_.emission  = std::max({gltf_emissive_[0], gltf_emissive_[1], gltf_emissive_[2]});
            fragment_.unlit     = gltf_unlit_;
            fragment_.material_texture_binds = tex_bind_group_;
            fragment_.pipeline_flags = vivid::gpu::kPipelineTextured;
        } else {
            // User-specified color (no glTF material)
            fragment_.color[0] = r.value;
            fragment_.color[1] = g.value;
            fragment_.color[2] = b.value;
            fragment_.color[3] = a.value;
            fragment_.roughness = 0.5f;
            fragment_.metallic  = 0.0f;
            fragment_.emission  = 0.0f;
            fragment_.unlit     = false;
            fragment_.material_texture_binds = nullptr;
            fragment_.pipeline_flags = 0;
        }

        gpu->output_scene = &fragment_;
    }

    ~MeshImport() override {
        vivid::gpu::release(vertex_buffer_);
        vivid::gpu::release(index_buffer_);
        release_textures();
    }

private:
    vivid::gpu::VividSceneFragment fragment_{};
    WGPUBuffer   vertex_buffer_  = nullptr;
    WGPUBuffer   index_buffer_   = nullptr;
    uint64_t     vertex_buf_size_ = 0;
    uint32_t     index_count_     = 0;
    std::string  cached_path_;
    std::vector<vivid::gpu::Vertex3D> cpu_verts_;
    std::vector<uint32_t> cpu_indices_;

    // glTF PBR material scalars
    bool  has_gltf_material_ = false;
    float gltf_base_color_[4] = {1,1,1,1};
    float gltf_roughness_ = 1.0f;
    float gltf_metallic_  = 1.0f;
    float gltf_emissive_[3] = {0,0,0};
    bool  gltf_unlit_ = false;

    // Decoded image data (CPU side, freed after GPU upload)
    // Indices: 0=albedo, 1=normal, 2=roughnessMetallic, 3=emission
    struct ImageData { std::vector<uint8_t> pixels; int w = 0, h = 0; };
    ImageData loaded_images_[4];
    bool has_rm_texture_ = false;

    // GPU texture resources
    bool textures_inited_ = false;
    WGPUTexture     gpu_textures_[4]      = {};
    WGPUTextureView gpu_views_[4]         = {};
    WGPUTexture     fallback_textures_[4] = {};
    WGPUTextureView fallback_views_[4]    = {};
    WGPUSampler     sampler_              = nullptr;
    WGPUBindGroupLayout tex_bind_layout_  = nullptr;
    WGPUBindGroup   tex_bind_group_       = nullptr;

    // =========================================================================
    // Texture GPU upload + bind group creation
    // =========================================================================

    void lazy_init_textures(VividGpuState* gpu) {
        sampler_ = vivid::gpu::create_repeat_sampler(gpu->device, "MeshImport Sampler");
        tex_bind_layout_ = vivid::gpu::create_pbr_texture_bind_layout(gpu->device);
        if (!sampler_ || !tex_bind_layout_) return;

        // Create 1x1 fallback textures (same as Material3D)
        struct FallbackSpec { const char* label; uint8_t rgba[4]; };
        FallbackSpec specs[4] = {
            {"MeshImport Fallback Albedo",   {255, 255, 255, 255}},
            {"MeshImport Fallback Normal",   {128, 128, 255, 255}},
            {"MeshImport Fallback R/M",      {255,   0,   0,   0}},
            {"MeshImport Fallback Emission", {  0,   0,   0,   0}},
        };
        for (int i = 0; i < 4; ++i) {
            WGPUTextureDescriptor td{};
            td.label = vivid_sv(specs[i].label);
            td.size = {1, 1, 1};
            td.mipLevelCount = 1;
            td.sampleCount = 1;
            td.dimension = WGPUTextureDimension_2D;
            td.format = WGPUTextureFormat_RGBA8Unorm;
            td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            fallback_textures_[i] = wgpuDeviceCreateTexture(gpu->device, &td);

            WGPUTexelCopyTextureInfo dst{};
            dst.texture = fallback_textures_[i];
            dst.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferLayout layout_info{};
            layout_info.bytesPerRow = 4;
            layout_info.rowsPerImage = 1;
            WGPUExtent3D extent = {1, 1, 1};
            wgpuQueueWriteTexture(gpu->queue, &dst, specs[i].rgba, 4, &layout_info, &extent);

            WGPUTextureViewDescriptor vd{};
            vd.label = vivid_sv(specs[i].label);
            vd.format = WGPUTextureFormat_RGBA8Unorm;
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = 1;
            fallback_views_[i] = wgpuTextureCreateView(fallback_textures_[i], &vd);
        }

        // Upload decoded images to GPU
        for (int i = 0; i < 4; ++i) {
            if (loaded_images_[i].pixels.empty()) continue;

            int w = loaded_images_[i].w;
            int h = loaded_images_[i].h;

            WGPUTextureDescriptor td{};
            td.label = vivid_sv("MeshImport glTF Tex");
            td.size = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
            td.mipLevelCount = 1;
            td.sampleCount = 1;
            td.dimension = WGPUTextureDimension_2D;
            td.format = WGPUTextureFormat_RGBA8Unorm;
            td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            gpu_textures_[i] = wgpuDeviceCreateTexture(gpu->device, &td);

            WGPUTexelCopyTextureInfo dst{};
            dst.texture = gpu_textures_[i];
            dst.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferLayout layout_info{};
            layout_info.bytesPerRow = static_cast<uint32_t>(w * 4);
            layout_info.rowsPerImage = static_cast<uint32_t>(h);
            WGPUExtent3D extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
            wgpuQueueWriteTexture(gpu->queue, &dst,
                                  loaded_images_[i].pixels.data(),
                                  loaded_images_[i].pixels.size(),
                                  &layout_info, &extent);

            WGPUTextureViewDescriptor vd{};
            vd.label = vivid_sv("MeshImport glTF TexView");
            vd.format = WGPUTextureFormat_RGBA8Unorm;
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = 1;
            gpu_views_[i] = wgpuTextureCreateView(gpu_textures_[i], &vd);

            // Free CPU pixels
            loaded_images_[i].pixels.clear();
            loaded_images_[i].pixels.shrink_to_fit();
        }

        // Build bind group: sampler + 4 texture views (GPU or fallback)
        WGPUBindGroupEntry entries[5]{};
        entries[0].binding = 0;
        entries[0].sampler = sampler_;
        for (int i = 0; i < 4; ++i) {
            entries[i + 1].binding = static_cast<uint32_t>(i + 1);
            entries[i + 1].textureView = gpu_views_[i] ? gpu_views_[i] : fallback_views_[i];
        }

        WGPUBindGroupDescriptor bgd{};
        bgd.label = vivid_sv("MeshImport Tex BG");
        bgd.layout = tex_bind_layout_;
        bgd.entryCount = 5;
        bgd.entries = entries;
        tex_bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bgd);

        textures_inited_ = true;
    }

    void release_textures() {
        vivid::gpu::release(tex_bind_group_);
        vivid::gpu::release(tex_bind_layout_);
        vivid::gpu::release(sampler_);
        for (int i = 0; i < 4; ++i) {
            vivid::gpu::release(gpu_views_[i]);
            vivid::gpu::release(gpu_textures_[i]);
            vivid::gpu::release(fallback_views_[i]);
            vivid::gpu::release(fallback_textures_[i]);
            loaded_images_[i].pixels.clear();
            loaded_images_[i].w = 0;
            loaded_images_[i].h = 0;
        }
        textures_inited_ = false;
        has_gltf_material_ = false;
        has_rm_texture_ = false;
        gltf_base_color_[0] = gltf_base_color_[1] = gltf_base_color_[2] = gltf_base_color_[3] = 1.0f;
        gltf_roughness_ = 1.0f;
        gltf_metallic_ = 1.0f;
        gltf_emissive_[0] = gltf_emissive_[1] = gltf_emissive_[2] = 0.0f;
        gltf_unlit_ = false;
    }

    // =========================================================================
    // Format detection
    // =========================================================================

    enum Format { FMT_OBJ, FMT_GLTF, FMT_UNKNOWN };
    static Format detect_format(const std::string& path) {
        auto dot = path.rfind('.');
        if (dot == std::string::npos) return FMT_UNKNOWN;
        std::string ext = path.substr(dot);
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".obj") return FMT_OBJ;
        if (ext == ".gltf" || ext == ".glb") return FMT_GLTF;
        return FMT_UNKNOWN;
    }

    void load_mesh(VividGpuState* gpu) {
        vivid::gpu::release(vertex_buffer_);
        vivid::gpu::release(index_buffer_);
        cpu_verts_.clear();
        cpu_indices_.clear();
        vertex_buf_size_ = 0;
        index_count_ = 0;

        if (cached_path_.empty()) return;

        std::vector<vivid::gpu::Vertex3D> verts;
        std::vector<uint32_t> indices;

        Format fmt = detect_format(cached_path_);
        bool ok = false;
        switch (fmt) {
            case FMT_OBJ:  ok = load_obj(cached_path_, verts, indices); break;
            case FMT_GLTF: ok = load_gltf(cached_path_, verts, indices); break;
            default: break;
        }

        if (!ok || verts.empty() || indices.empty()) {
            std::fprintf(stderr, "[mesh_import] Failed to load: %s\n", cached_path_.c_str());
            return;
        }

        cpu_verts_ = verts;
        cpu_indices_ = indices;
        vertex_buf_size_ = verts.size() * sizeof(vivid::gpu::Vertex3D);
        index_count_ = static_cast<uint32_t>(indices.size());

        vertex_buffer_ = vivid::gpu::create_vertex_buffer(
            gpu->device, gpu->queue, verts.data(), vertex_buf_size_, "MeshImport VB");
        index_buffer_ = vivid::gpu::create_index_buffer(
            gpu->device, gpu->queue, indices.data(), index_count_, "MeshImport IB");
    }

    // =========================================================================
    // OBJ loading via tinyobjloader
    // =========================================================================

    static bool load_obj(const std::string& path,
                         std::vector<vivid::gpu::Vertex3D>& out_verts,
                         std::vector<uint32_t>& out_indices) {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str());
        if (!ret || shapes.empty()) {
            if (!err.empty()) std::fprintf(stderr, "[mesh_import] OBJ error: %s\n", err.c_str());
            return false;
        }

        bool has_normals = !attrib.normals.empty();

        // Flatten all shapes into single vertex/index list
        for (auto& shape : shapes) {
            size_t idx_offset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
                int fv = shape.mesh.num_face_vertices[f];

                // Compute face normal if normals are missing
                float fn[3] = {0, 1, 0};
                if (!has_normals && fv >= 3) {
                    auto& i0 = shape.mesh.indices[idx_offset + 0];
                    auto& i1 = shape.mesh.indices[idx_offset + 1];
                    auto& i2 = shape.mesh.indices[idx_offset + 2];
                    float p0[3] = { attrib.vertices[3*i0.vertex_index+0],
                                    attrib.vertices[3*i0.vertex_index+1],
                                    attrib.vertices[3*i0.vertex_index+2] };
                    float p1[3] = { attrib.vertices[3*i1.vertex_index+0],
                                    attrib.vertices[3*i1.vertex_index+1],
                                    attrib.vertices[3*i1.vertex_index+2] };
                    float p2[3] = { attrib.vertices[3*i2.vertex_index+0],
                                    attrib.vertices[3*i2.vertex_index+1],
                                    attrib.vertices[3*i2.vertex_index+2] };
                    float e1[3] = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
                    float e2[3] = { p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
                    fn[0] = e1[1]*e2[2] - e1[2]*e2[1];
                    fn[1] = e1[2]*e2[0] - e1[0]*e2[2];
                    fn[2] = e1[0]*e2[1] - e1[1]*e2[0];
                    float len = std::sqrt(fn[0]*fn[0] + fn[1]*fn[1] + fn[2]*fn[2]);
                    if (len > 1e-8f) { fn[0]/=len; fn[1]/=len; fn[2]/=len; }
                }

                // Compute face tangent from first triangle's UV deltas
                float ft[3] = {1.0f, 0.0f, 0.0f};  // fallback tangent
                if (fv >= 3 && !attrib.texcoords.empty()) {
                    auto& ti0 = shape.mesh.indices[idx_offset + 0];
                    auto& ti1 = shape.mesh.indices[idx_offset + 1];
                    auto& ti2 = shape.mesh.indices[idx_offset + 2];
                    if (ti0.texcoord_index >= 0 && ti1.texcoord_index >= 0 && ti2.texcoord_index >= 0) {
                        float p0[3] = { attrib.vertices[3*ti0.vertex_index+0],
                                        attrib.vertices[3*ti0.vertex_index+1],
                                        attrib.vertices[3*ti0.vertex_index+2] };
                        float p1[3] = { attrib.vertices[3*ti1.vertex_index+0],
                                        attrib.vertices[3*ti1.vertex_index+1],
                                        attrib.vertices[3*ti1.vertex_index+2] };
                        float p2[3] = { attrib.vertices[3*ti2.vertex_index+0],
                                        attrib.vertices[3*ti2.vertex_index+1],
                                        attrib.vertices[3*ti2.vertex_index+2] };
                        float uv0[2] = { attrib.texcoords[2*ti0.texcoord_index+0],
                                         attrib.texcoords[2*ti0.texcoord_index+1] };
                        float uv1[2] = { attrib.texcoords[2*ti1.texcoord_index+0],
                                         attrib.texcoords[2*ti1.texcoord_index+1] };
                        float uv2[2] = { attrib.texcoords[2*ti2.texcoord_index+0],
                                         attrib.texcoords[2*ti2.texcoord_index+1] };
                        float e1[3] = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
                        float e2[3] = { p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
                        float duv1[2] = { uv1[0]-uv0[0], uv1[1]-uv0[1] };
                        float duv2[2] = { uv2[0]-uv0[0], uv2[1]-uv0[1] };
                        float det = duv1[0]*duv2[1] - duv1[1]*duv2[0];
                        if (std::fabs(det) > 1e-8f) {
                            float inv = 1.0f / det;
                            ft[0] = inv * (duv2[1]*e1[0] - duv1[1]*e2[0]);
                            ft[1] = inv * (duv2[1]*e1[1] - duv1[1]*e2[1]);
                            ft[2] = inv * (duv2[1]*e1[2] - duv1[1]*e2[2]);
                            float tlen = std::sqrt(ft[0]*ft[0] + ft[1]*ft[1] + ft[2]*ft[2]);
                            if (tlen > 1e-8f) { ft[0]/=tlen; ft[1]/=tlen; ft[2]/=tlen; }
                            else { ft[0]=1; ft[1]=0; ft[2]=0; }
                        }
                    }
                }

                // Triangulate fan-style for faces with > 3 vertices
                for (int v = 0; v < fv - 2; ++v) {
                    int tri[3] = { 0, v + 1, v + 2 };
                    for (int t = 0; t < 3; ++t) {
                        auto& idx = shape.mesh.indices[idx_offset + tri[t]];
                        vivid::gpu::Vertex3D vert{};

                        vert.position[0] = attrib.vertices[3*idx.vertex_index+0];
                        vert.position[1] = attrib.vertices[3*idx.vertex_index+1];
                        vert.position[2] = attrib.vertices[3*idx.vertex_index+2];

                        if (has_normals && idx.normal_index >= 0) {
                            vert.normal[0] = attrib.normals[3*idx.normal_index+0];
                            vert.normal[1] = attrib.normals[3*idx.normal_index+1];
                            vert.normal[2] = attrib.normals[3*idx.normal_index+2];
                        } else {
                            vert.normal[0] = fn[0];
                            vert.normal[1] = fn[1];
                            vert.normal[2] = fn[2];
                        }

                        vert.tangent[0] = ft[0];
                        vert.tangent[1] = ft[1];
                        vert.tangent[2] = ft[2];
                        vert.tangent[3] = 1.0f;

                        if (!attrib.texcoords.empty() && idx.texcoord_index >= 0) {
                            vert.uv[0] = attrib.texcoords[2*idx.texcoord_index+0];
                            vert.uv[1] = attrib.texcoords[2*idx.texcoord_index+1];
                        }

                        out_indices.push_back(static_cast<uint32_t>(out_verts.size()));
                        out_verts.push_back(vert);
                    }
                }
                idx_offset += fv;
            }
        }
        return true;
    }

    // =========================================================================
    // glTF loading via cgltf — geometry + PBR material extraction
    // =========================================================================

    // Decode a glTF texture image via stb_image into RGBA pixels.
    static bool decode_gltf_image(const cgltf_texture_view& tv, ImageData& out) {
        if (!tv.texture || !tv.texture->image) return false;
        cgltf_image* img = tv.texture->image;

        const uint8_t* raw_data = nullptr;
        size_t raw_size = 0;

        if (img->buffer_view) {
            // Embedded (GLB) or buffer-referenced image
            raw_data = static_cast<const uint8_t*>(cgltf_buffer_view_data(img->buffer_view));
            raw_size = img->buffer_view->size;
        } else if (img->uri) {
            // External URI — not supported in this path (would need file I/O relative to .gltf)
            return false;
        }

        if (!raw_data || raw_size == 0) return false;

        int w, h, channels;
        uint8_t* pixels = stbi_load_from_memory(raw_data, static_cast<int>(raw_size),
                                                 &w, &h, &channels, 4);
        if (!pixels) return false;

        out.w = w;
        out.h = h;
        out.pixels.assign(pixels, pixels + w * h * 4);
        stbi_image_free(pixels);
        return true;
    }

    bool load_gltf(const std::string& path,
                   std::vector<vivid::gpu::Vertex3D>& out_verts,
                   std::vector<uint32_t>& out_indices) {
        cgltf_options options{};
        cgltf_data* data = nullptr;

        cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
        if (result != cgltf_result_success) {
            std::fprintf(stderr, "[mesh_import] glTF parse failed: %d\n", static_cast<int>(result));
            return false;
        }

        result = cgltf_load_buffers(&options, data, path.c_str());
        if (result != cgltf_result_success) {
            std::fprintf(stderr, "[mesh_import] glTF buffer load failed\n");
            cgltf_free(data);
            return false;
        }

        bool material_extracted = false;

        // Iterate all meshes/primitives
        for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
            cgltf_mesh& mesh = data->meshes[mi];
            for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
                cgltf_primitive& prim = mesh.primitives[pi];
                if (prim.type != cgltf_primitive_type_triangles) continue;

                // Find accessors
                cgltf_accessor* pos_acc = nullptr;
                cgltf_accessor* norm_acc = nullptr;
                cgltf_accessor* tan_acc = nullptr;
                cgltf_accessor* uv_acc = nullptr;

                for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                    if (prim.attributes[ai].type == cgltf_attribute_type_position)
                        pos_acc = prim.attributes[ai].data;
                    else if (prim.attributes[ai].type == cgltf_attribute_type_normal)
                        norm_acc = prim.attributes[ai].data;
                    else if (prim.attributes[ai].type == cgltf_attribute_type_tangent)
                        tan_acc = prim.attributes[ai].data;
                    else if (prim.attributes[ai].type == cgltf_attribute_type_texcoord)
                        uv_acc = prim.attributes[ai].data;
                }

                if (!pos_acc) continue;

                uint32_t vert_base = static_cast<uint32_t>(out_verts.size());
                cgltf_size vert_count = pos_acc->count;

                // Read vertices
                for (cgltf_size vi = 0; vi < vert_count; ++vi) {
                    vivid::gpu::Vertex3D v{};
                    cgltf_accessor_read_float(pos_acc, vi, v.position, 3);
                    if (norm_acc)
                        cgltf_accessor_read_float(norm_acc, vi, v.normal, 3);
                    else
                        v.normal[1] = 1.0f;  // default up
                    if (tan_acc)
                        cgltf_accessor_read_float(tan_acc, vi, v.tangent, 4);
                    else {
                        v.tangent[0] = 1.0f; v.tangent[1] = 0.0f;
                        v.tangent[2] = 0.0f; v.tangent[3] = 1.0f;
                    }
                    if (uv_acc)
                        cgltf_accessor_read_float(uv_acc, vi, v.uv, 2);
                    out_verts.push_back(v);
                }

                // Read indices
                if (prim.indices) {
                    for (cgltf_size ii = 0; ii < prim.indices->count; ++ii) {
                        uint32_t idx = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, ii));
                        out_indices.push_back(vert_base + idx);
                    }
                } else {
                    for (cgltf_size vi = 0; vi < vert_count; ++vi) {
                        out_indices.push_back(vert_base + static_cast<uint32_t>(vi));
                    }
                }

                // Extract PBR material from the first primitive that has one
                if (!material_extracted && prim.material) {
                    extract_gltf_material(prim.material);
                    material_extracted = true;
                }
            }
        }

        cgltf_free(data);
        return !out_verts.empty();
    }

    void extract_gltf_material(const cgltf_material* mat) {
        if (mat->has_pbr_metallic_roughness) {
            auto& pbr = mat->pbr_metallic_roughness;
            std::memcpy(gltf_base_color_, pbr.base_color_factor, sizeof(float) * 4);
            gltf_roughness_ = pbr.roughness_factor;
            gltf_metallic_  = pbr.metallic_factor;
            has_gltf_material_ = true;

            // Decode albedo (baseColorTexture)
            decode_gltf_image(pbr.base_color_texture, loaded_images_[0]);

            // Decode roughness/metallic texture
            // glTF: G=roughness, B=metallic. Shader expects R=roughness, G=metallic.
            if (decode_gltf_image(pbr.metallic_roughness_texture, loaded_images_[2])) {
                swizzle_roughness_metallic(loaded_images_[2], gltf_metallic_);
                has_rm_texture_ = true;
            }
        }

        // Normal map
        decode_gltf_image(mat->normal_texture, loaded_images_[1]);

        // Emissive
        std::memcpy(gltf_emissive_, mat->emissive_factor, sizeof(float) * 3);
        decode_gltf_image(mat->emissive_texture, loaded_images_[3]);

        gltf_unlit_ = mat->unlit;
    }

    // Remap glTF metallic-roughness channels:
    // glTF: R=unused, G=roughness, B=metallic, A=unused
    // Shader: R=roughness (multiplicative), G=metallic (additive), B=0, A=0
    // Bake metallic_factor into the texture since the shader adds (not multiplies)
    // the scalar metallic uniform.
    static void swizzle_roughness_metallic(ImageData& img, float metallic_factor) {
        size_t pixel_count = static_cast<size_t>(img.w) * img.h;
        for (size_t i = 0; i < pixel_count; ++i) {
            uint8_t* p = &img.pixels[i * 4];
            uint8_t roughness = p[1];  // G channel
            uint8_t metallic  = static_cast<uint8_t>(p[2] * metallic_factor);  // B channel * factor
            p[0] = roughness;
            p[1] = metallic;
            p[2] = 0;
            p[3] = 0;
        }
    }
};

VIVID_REGISTER(MeshImport)
