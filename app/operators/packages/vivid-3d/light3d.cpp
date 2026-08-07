#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include "operator_api/thumbnail_3d.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// =============================================================================
// Light3D — light source as a scene element
// =============================================================================

/**
 * @brief Defines a configurable 3D light source for the scene pipeline.
 *
 * Light3D creates directional, point, or spot lights with adjustable color, placement, and
 * intensity so scenes can be lit without embedding light logic inside geometry operators.
 *
 * ADR-0051 Phase 1 — ONE rule for aim, one for placement:
 *   - `dir_*` is the direction the light SHINES (pointing away from it). Directional and Spot
 *     use it; Point ignores it.
 *   - `pos_*` is where the light IS. Point and Spot use it; Directional ignores it (a
 *     directional light is infinitely far away, so it has no position — only an aim).
 * Before ADR-0051 a directional light took its direction from `pos_*` and silently ignored
 * `dir_*`, which meant every hand-authored key light in the shipped demos had no effect. A
 * pre-v5 session migrates on load (see `migrate_node_params` in `app/src/persist.h`).
 *
 * @param type Light type: directional, point, or spot.
 * @param intensity Overall light intensity.
 * @param radius Influence radius for local lights.
 * @param pos_x Light position along the X axis (Point/Spot).
 * @param dir_y Y component of the direction the light shines (Directional/Spot).
 * @param spot_angle Cone angle for spot lights.
 */
struct Light3D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Light3D";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;   // ADR-0046
    static constexpr bool kTimeDependent = false;

    // `type` choice indices — the wire values Render3D switches on (see VividSceneFragment::light_type).
    static constexpr int kDirectional = 0;
    static constexpr int kPoint       = 1;
    static constexpr int kSpot        = 2;

    vivid::Param<int>   type      {"type",      kDirectional, {"Directional", "Point", "Spot"}};
    vivid::Param<float> intensity {"intensity", 1.0f, 0.0f, 10.0f};
    vivid::Param<float> r         {"r",         1.0f, 0.0f, 1.0f};
    vivid::Param<float> g         {"g",         1.0f, 0.0f, 1.0f};
    vivid::Param<float> b         {"b",         1.0f, 0.0f, 1.0f};
    vivid::Param<float> radius    {"radius",   10.0f, 0.1f, 100.0f};
    vivid::Param<float> pos_x     {"pos_x",     0.5f, -50.0f, 50.0f};
    vivid::Param<float> pos_y     {"pos_y",     1.0f, -50.0f, 50.0f};
    vivid::Param<float> pos_z     {"pos_z",     0.8f, -50.0f, 50.0f};

    // The direction the light SHINES (Directional + Spot). The defaults are -normalize(0.5, 1, 0.8):
    // the exact key direction Render3D's built-in no-light fallback uses, so a freshly-added
    // Light3D still lights a scene identically to having no light at all — an invariant worth
    // keeping, and a nicer 3/4 key than straight-down would be.
    vivid::Param<float> dir_x     {"dir_x",    -0.363696f, -1.0f, 1.0f};
    vivid::Param<float> dir_y     {"dir_y",    -0.727393f, -1.0f, 1.0f};
    vivid::Param<float> dir_z     {"dir_z",    -0.581914f, -1.0f, 1.0f};

    // Spot light cone params
    vivid::Param<float> spot_angle {"spot_angle", 45.0f, 5.0f, 90.0f};
    vivid::Param<float> spot_blend {"spot_blend", 0.1f, 0.0f, 1.0f};

    // ADR-0051 Phase 2: whether this light casts. On by default so existing scenes are unchanged;
    // turn it off for a fill light, which should lift the shadows a key light casts, not add its own.
    // Point lights never cast regardless (an omni shadow needs a cube map — Phase 2c).
    vivid::Param<int>   cast_shadow {"cast_shadow", 1, {"Off", "On"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(type, "Light");
        vivid::param_group(intensity, "Light");
        vivid::param_group(radius, "Light");

        vivid::param_group(r, "Color");
        vivid::param_group(g, "Color");
        vivid::param_group(b, "Color");
        vivid::display_hint(r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(b, VIVID_DISPLAY_COLOR);

        // ADR-0051: declare which params each type actually reads. NOTE: these predicates reach the
        // C descriptor (VIVID_REGISTER copies them) and are readable over the control server, but
        // no UI consumes them yet — the param panel lays out by param INDEX, so honouring them
        // needs a visible-index mapping shared by layout + draw + hit-test. That is a change for
        // every operator, not a lighting one; ADR-0051 Phase 1b tracks it. Declared now so the
        // intent is recorded where it belongs and lights up when the consumer lands.
        // `radius` attenuates local lights, so it is meaningless on a directional one.
        vivid::visible_when_ne(radius, type, kDirectional);

        // Only directional + spot can cast (Phase 2c would add omni cube shadows).
        vivid::param_group(cast_shadow, "Light");
        vivid::visible_when_ne(cast_shadow, type, kPoint);

        // Position: Point + Spot. A directional light has no position.
        vivid::param_group(pos_x, "Position");
        vivid::param_group(pos_y, "Position");
        vivid::param_group(pos_z, "Position");
        vivid::visible_when_ne(pos_x, type, kDirectional);
        vivid::visible_when_ne(pos_y, type, kDirectional);
        vivid::visible_when_ne(pos_z, type, kDirectional);

        // Direction: Directional + Spot. A point light shines every way at once.
        vivid::param_group(dir_x, "Direction");
        vivid::param_group(dir_y, "Direction");
        vivid::param_group(dir_z, "Direction");
        vivid::visible_when_ne(dir_x, type, kPoint);
        vivid::visible_when_ne(dir_y, type, kPoint);
        vivid::visible_when_ne(dir_z, type, kPoint);

        vivid::param_group(spot_angle, "Spot");
        vivid::param_group(spot_blend, "Spot");
        vivid::visible_when_eq(spot_angle, type, kSpot);
        vivid::visible_when_eq(spot_blend, type, kSpot);

        out.push_back(&type);
        out.push_back(&intensity);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&radius);
        out.push_back(&pos_x);
        out.push_back(&pos_y);
        out.push_back(&pos_z);
        out.push_back(&dir_x);
        out.push_back(&dir_y);
        out.push_back(&dir_z);
        out.push_back(&spot_angle);
        out.push_back(&spot_blend);
        out.push_back(&cast_shadow);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::scene_port("scene", VIVID_PORT_OUTPUT));
    }

    void draw_thumbnail(const VividThumbnailContext*) override { /* TODO(ADR-0041 Phase 1): reimplement against trunk 2D VividDrawAPI */ }

    void process_gpu(const VividGpuContext* ctx) override {
        fragment_.fragment_type   = vivid::gpu::VividSceneFragment::LIGHT;
        fragment_.light_type      = static_cast<float>(type.int_value());
        fragment_.light_color[0]  = r.value;
        fragment_.light_color[1]  = g.value;
        fragment_.light_color[2]  = b.value;
        fragment_.light_intensity = intensity.value;
        fragment_.light_radius    = radius.value;

        // Spot light params
        fragment_.light_direction[0] = dir_x.value;
        fragment_.light_direction[1] = dir_y.value;
        fragment_.light_direction[2] = dir_z.value;
        fragment_.light_spot_angle   = spot_angle.value;
        fragment_.light_spot_blend   = spot_blend.value;
        fragment_.light_cast_shadow  = (cast_shadow.int_value() != 0);

        // Position/direction encoded in model_matrix translation
        mat4x4_translate(fragment_.model_matrix, pos_x.value, pos_y.value, pos_z.value);

        // No geometry
        fragment_.vertex_buffer   = nullptr;
        fragment_.vertex_buf_size = 0;
        fragment_.index_buffer    = nullptr;
        fragment_.index_count     = 0;
        fragment_.pipeline        = nullptr;
        fragment_.material_binds  = nullptr;
        fragment_.children        = nullptr;
        fragment_.child_count     = 0;

        ctx->custom_outputs[0] = &fragment_;

        // Animated 3D thumbnail: a glowing proxy sphere in the light's colour.
        const float col[3] = { r.value, g.value, b.value };
        vivid::thumb3d::render_proxy_sphere(ctx, thumb_, col, true);
    }

    ~Light3D() override { vivid::thumb3d::destroy(thumb_); }

private:
    vivid::gpu::VividSceneFragment fragment_{};
    vivid::thumb3d::State thumb_{};
};

VIVID_REGISTER(Light3D)
VIVID_THUMBNAIL(Light3D)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividSceneFragment)
