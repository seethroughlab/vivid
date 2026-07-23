// Core visual package operator: MeshRender — take a MESH VALUE (custom-ref input) and render it to a
// texture with a spinning camera, tint and flat lighting. The sink of the composable geometry
// pipeline (MeshLoad -> MeshDisplace -> MeshRender); it turns geometry back into an image the rest
// of the visuals graph composites. Flat-lit (white x tint x shade), no baseColor — the mesh value
// carries geometry only; a textured variant (baseColor via a texture input) is a future addition.
//
// It shares the exact render pipeline Model uses (operator_api/mesh_render.h), so Model and the
// composable path draw geometry identically. A processor (one mesh input) — clears transparent when
// nothing is wired in, so it composites cleanly.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/geom.h"          // input_mesh
#include "operator_api/mesh_render.h"   // MeshRenderer

#include <array>
#include <cmath>
#include <string>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
}  // namespace

struct MeshRenderOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "MeshRender";
    static constexpr const char* kDisplayName = "Mesh Render";
    static constexpr const char* kSummary = "Render a mesh (from the geometry pipeline) to a textured, lit image.";
    static constexpr std::array<const char*, 2> kKeywords = {"geometry", "3d"};

    vivid::Param<float> size{"size", 0.55f, 0.f, 1.f};
    vivid::Param<float> spin{"spin", 0.3f, 0.f, 1.f}, tilt{"tilt", 0.5f, 0.f, 1.f};
    vivid::Param<float> light{"light", 0.35f, 0.f, 1.f};
    vivid::Param<float> r{"r", 1.f, 0.f, 1.f}, g{"g", 1.f, 0.f, 1.f}, b{"b", 1.f, 0.f, 1.f};
    vivid::Param<float> bg_r{"bg_r", 0.f, 0.f, 1.f}, bg_g{"bg_g", 0.f, 0.f, 1.f}, bg_b{"bg_b", 0.f, 0.f, 1.f};

    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR; bg_r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&size); o.push_back(&spin); o.push_back(&tilt); o.push_back(&light);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&bg_r); o.push_back(&bg_g); o.push_back(&bg_b);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(VIVID_CUSTOM_REF_PORT("mesh", VIVID_PORT_INPUT, VividMesh));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }

    ~MeshRenderOp() override { renderer_.release(); }

    void process_gpu(const VividGpuContext* c) override {
        if (init_failed_) { vivid_report_gpu_error(c, err_.c_str()); return; }
        if (!renderer_.ready()) { if (!renderer_.init(c)) { init_failed_ = true; err_ = renderer_.err_; vivid_report_gpu_error(c, err_.c_str()); return; } }

        const float* p = c->param_values; auto pv = [&](int i, float d){ return p ? p[i] : d; };
        const auto tf = vivid::geom::spinning_camera(pv(0, size.value), pv(1, spin.value), pv(2, tilt.value),
            float(c->time), float(c->output_width) / std::max(1.f, float(c->output_height)));
        const float lightv[4] = { 0.4f, 0.7f, 0.55f, pv(3, light.value) };
        const float misc[3]   = { 0.f, 1.f, 0.f };   // no vertex-shader displacement (that's MeshDisplace's job)

        const VividMesh* m = vivid::geom::input_mesh(c, 0);
        VividMesh empty{};   // render() clears to the bg even with no geometry -> a clean transparent frame
        renderer_.render(c, m ? *m : empty, tf, pv(4, r.value), pv(5, g.value), pv(6, b.value),
                         lightv, misc, pv(7, bg_r.value), pv(8, bg_g.value), pv(9, bg_b.value),
                         m ? m->base_color : nullptr);   // the mesh carries its baseColor material (null -> flat white)
    }

private:
    vivid::geom::MeshRenderer renderer_;
    bool init_failed_ = false; std::string err_;
};

VIVID_REGISTER(MeshRenderOp)
