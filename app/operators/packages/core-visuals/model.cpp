// Core visual package operator: Model — load a glTF / GLB 3D model file into the chain and render
// it as a spinning, textured, flat-lit solid. A generator (inputs:[]) — it clears to TRANSPARENT so
// the model composites as an overlay in the graph.
//
// Model is the all-in-one convenience node: file -> geometry + baseColor -> render, in one op. It
// now stands on the SHARED geometry library (operator_api/gltf_mesh.h + mesh_render.h) rather than a
// private copy of the parser + pipeline — the same helpers the composable MeshLoad / MeshRender /
// MeshDisplace ops use, so a fix to the parser or the render path lands everywhere at once. Model
// keeps its in-vertex-shader 3D-noise displacement (the `noise`/`nscale` params -> the renderer's
// misc uniform); the composable path does displacement as a real mesh-modifying node instead.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"

#define STB_IMAGE_IMPLEMENTATION            // this TU owns the stb_image implementation for the parser
#include "operator_api/gltf_mesh.h"         // CPU glTF parse + Mat4 (pulls in stb_image.h)
#include "operator_api/mesh_render.h"       // GpuMesh upload + MeshRenderer + BaseColorTexture

#include <array>
#include <cstdio>
#include <string>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
}  // namespace

struct ModelOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Model";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;   // ADR-0046
    static constexpr const char* kDisplayName = "Model";
    static constexpr const char* kSummary = "Load a 3D model (glTF/GLB) from a file and render it textured + lit.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "geometry", "3d"};

    vivid::Param<vivid::FilePath> path{"file", ""};
    vivid::Param<float> size{"size", 0.55f, 0.f, 1.f};
    vivid::Param<float> spin{"spin", 0.3f, 0.f, 1.f}, tilt{"tilt", 0.5f, 0.f, 1.f};
    vivid::Param<float> light{"light", 0.35f, 0.f, 1.f};   // ambient floor (1 => flat, unlit)
    vivid::Param<float> r{"r", 1.f, 0.f, 1.f}, g{"g", 1.f, 0.f, 1.f}, b{"b", 1.f, 0.f, 1.f};   // tint multiply
    vivid::Param<float> bg_r{"bg_r", 0.f, 0.f, 1.f}, bg_g{"bg_g", 0.f, 0.f, 1.f}, bg_b{"bg_b", 0.f, 0.f, 1.f};
    vivid::Param<float> noise{"noise", 0.f, 0.f, 1.f};    // 3D vertex-displacement amount (map an instrument here)
    vivid::Param<float> nscale{"nscale", 0.4f, 0.f, 1.f}; // displacement spatial frequency (coarse..fine)

    ModelOp() {
        vivid::description(path, "3D model file to load (glTF .gltf / .glb)");
        vivid::asset_kind(path, "model");
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR; bg_r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&path); o.push_back(&size); o.push_back(&spin); o.push_back(&tilt); o.push_back(&light);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&bg_r); o.push_back(&bg_g); o.push_back(&bg_b);
        o.push_back(&noise); o.push_back(&nscale);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }

    ~ModelOp() override { gpu_.release(); tex_.release(); renderer_.release(); }

    void process_gpu(const VividGpuContext* c) override {
        if (init_failed_) { vivid_report_gpu_error(c, err_.c_str()); return; }
        if (!renderer_.ready()) { if (!renderer_.init(c)) { init_failed_ = true; err_ = renderer_.err_; vivid_report_gpu_error(c, err_.c_str()); return; } }
        if (path.str_value != loaded_path_) { loaded_path_ = path.str_value; load_model(c); }

        const float* p = c->param_values; auto pv = [&](int i, float d){ return p ? p[i] : d; };
        const auto tf = vivid::geom::spinning_camera(pv(1, size.value), pv(2, spin.value), pv(3, tilt.value),
            float(c->time), float(c->output_width) / std::max(1.f, float(c->output_height)));
        const float lightv[4] = { 0.4f, 0.7f, 0.55f, pv(4, light.value) };
        const float misc[3]   = { pv(11, noise.value) * 0.4f, 1.5f + pv(12, nscale.value) * 6.5f, float(c->time) * 0.5f };
        renderer_.render(c, gpu_.mesh, tf, pv(5, r.value), pv(6, g.value), pv(7, b.value),
                         lightv, misc, pv(8, bg_r.value), pv(9, bg_g.value), pv(10, bg_b.value), tex_.view());
    }

private:
    vivid::geom::MeshRenderer     renderer_;
    vivid::geom::GpuMesh          gpu_;
    vivid::geom::BaseColorTexture tex_;
    std::string loaded_path_ = "\x01";   // sentinel != any path so the first load fires (even empty -> fallback)
    bool init_failed_ = false; std::string err_;

    void load_model(const VividGpuContext* c) {
        gpu_.release(); tex_.release();
        vivid::geom::CpuMesh md; std::string e;
        if (loaded_path_.empty() || !vivid::geom::load_gltf(loaded_path_, md, e)) {
            if (!loaded_path_.empty()) std::fprintf(stderr, "[Model] load failed (%s): %s\n", loaded_path_.c_str(), e.c_str());
            return;   // no geometry -> nothing draws, node still valid (renderer uses its white fallback)
        }
        gpu_.upload(c, md);
        if (!md.tex_rgba.empty()) tex_.set(c, md.tex_rgba.data(), (uint32_t)md.tw, (uint32_t)md.th);
        std::fprintf(stderr, "[Model] loaded %s: %u verts, %u indices, tex %dx%d\n",
                     loaded_path_.c_str(), gpu_.vert_n, gpu_.index_n, md.tw, md.th);
    }
};

VIVID_REGISTER(ModelOp)

// ADR-0021/P3: drop a .gltf/.glb onto the graph -> a Model node with its "file" param set.
static const char* const kModelDropExts[] = { ".gltf", ".glb" };
static const VividFileDropHandlerDescriptor kModelDrop[] = {
    { "3D Model", kModelDropExts, 2, "file", 10, "Load as a 3D model" }
};
VIVID_FILE_DROP(kModelDrop)
