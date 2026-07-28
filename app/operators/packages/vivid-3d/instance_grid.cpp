#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include "operator_api/instance_algorithms.h"
#include "operator_api/thumbnail_3d.h"
#include <vector>

// =============================================================================
// InstanceGrid — emit InstanceArray3D for Grid / Circle / Line / Grid3D layouts
// =============================================================================

/**
 * @brief Generate N per-instance transforms in a geometric layout.
 *
 * Emits an InstanceArray3D bundle suitable for wiring into Instancer3D's
 * `instances` input. Layouts: Grid (2D), Circle (XZ ring), Line (X axis),
 * Grid3D (cubic lattice). Palette applies per-instance RGBA colors.
 *
 * @param count   Number of instances to generate (1–4096).
 * @param layout  Placement pattern.
 * @param spacing Distance between generated instances.
 * @param palette Built-in color palette (None, Warm, Cool, Neon).
 */
struct InstanceGrid : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName         = "InstanceGrid";
    static constexpr bool kTimeDependent       = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_GENERATE;

    vivid::Param<int>   count   {"count",   16, 1, 4096};
    vivid::Param<int>   layout  {"layout",  0, {"Grid", "Circle", "Line", "Grid3D"}};
    vivid::Param<float> spacing {"spacing", 2.0f, 0.1f, 20.0f};
    vivid::Param<int>   palette {"palette", 0, {"None", "Warm", "Cool", "Neon"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(count,   "Layout");
        vivid::param_group(layout,  "Layout");
        vivid::param_group(spacing, "Layout");
        vivid::param_group(palette, "Color");
        out.push_back(&count);
        out.push_back(&layout);
        out.push_back(&spacing);
        out.push_back(&palette);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("instances", VIVID_PORT_OUTPUT,
                                            vivid::gpu::InstanceArray3D));
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override { /* TODO(ADR-0041 Phase 1): reimplement vs trunk 2D VividDrawAPI */ }

    void process_gpu(const VividGpuContext* ctx) override {
        uint32_t n = static_cast<uint32_t>(count.int_value());
        if (n == 0) n = 1;
        if (n > 4096) n = 4096;

        instances_.resize(n);
        int    layout_mode = layout.int_value();
        int    pal         = palette.int_value();
        float  sp          = spacing.value;

        // Positions per layout (math lives in instance_algorithms.h).
        // 3D lays Grid/Circle/Line in the XZ plane (y-up floor); Grid3D is cubic.
        for (uint32_t i = 0; i < n; ++i) {
            switch (layout_mode) {
                case 1: {  // Circle (XZ ring)
                    auto p = vivid::instancing::circle_2d(i, n, sp);
                    instances_[i].position[0] = p.x;
                    instances_[i].position[1] = 0.0f;
                    instances_[i].position[2] = p.y;
                    break;
                }
                case 2: {  // Line (X axis)
                    auto p = vivid::instancing::line_2d(i, n, sp);
                    instances_[i].position[0] = p.x;
                    instances_[i].position[1] = 0.0f;
                    instances_[i].position[2] = 0.0f;
                    break;
                }
                case 3: {  // Grid3D — cubic lattice
                    auto p = vivid::instancing::grid_3d(i, n, sp);
                    instances_[i].position[0] = p.x;
                    instances_[i].position[1] = p.y;
                    instances_[i].position[2] = p.z;
                    break;
                }
                default: {  // Grid (2D, XZ plane)
                    auto p = vivid::instancing::grid_2d(i, n, sp);
                    instances_[i].position[0] = p.x;
                    instances_[i].position[1] = 0.0f;
                    instances_[i].position[2] = p.y;
                    break;
                }
            }
        }

        // Defaults for the other fields + palette color
        for (uint32_t i = 0; i < n; ++i) {
            instances_[i].rotation_y = 0.0f;
            instances_[i].rotation_x = 0.0f;
            instances_[i].scale[0] = 1.0f;
            instances_[i].scale[1] = 1.0f;
            instances_[i].scale[2] = 1.0f;
            if (pal > 0) {
                apply_palette(instances_[i].color, i, pal);
            } else {
                instances_[i].color[0] = 1.0f;
                instances_[i].color[1] = 1.0f;
                instances_[i].color[2] = 1.0f;
                instances_[i].color[3] = 1.0f;
            }
        }

        bundle_.data  = instances_.data();
        bundle_.count = n;
        ctx->custom_outputs[0] = &bundle_;

        // Animated 3D thumbnail: a unit cube instanced at the generated transforms.
        vivid::thumb3d::render_instances_cpu(ctx, thumb_, instances_.data(), n);
    }

    ~InstanceGrid() override { vivid::thumb3d::destroy(thumb_); }

private:
    std::vector<vivid::gpu::InstanceData3D> instances_;
    vivid::gpu::InstanceArray3D bundle_{};
    vivid::thumb3d::State thumb_{};

    static void apply_palette(float color[4], uint32_t index, int palette_id) {
        static constexpr float kWarm[3][3] = {
            {1.0f, 0.35f, 0.25f},
            {1.0f, 0.75f, 0.2f},
            {0.95f, 0.5f, 0.6f},
        };
        static constexpr float kCool[3][3] = {
            {0.2f, 0.6f, 1.0f},
            {0.3f, 0.9f, 0.7f},
            {0.55f, 0.35f, 1.0f},
        };
        static constexpr float kNeon[3][3] = {
            {1.0f, 0.1f, 0.5f},
            {0.1f, 1.0f, 0.6f},
            {0.2f, 0.5f, 1.0f},
        };
        const float (*pal)[3] = kWarm;
        if (palette_id == 2) pal = kCool;
        else if (palette_id == 3) pal = kNeon;
        uint32_t ci = index % 3;
        color[0] = pal[ci][0];
        color[1] = pal[ci][1];
        color[2] = pal[ci][2];
        color[3] = 1.0f;
    }
};

VIVID_REGISTER(InstanceGrid)
VIVID_THUMBNAIL(InstanceGrid)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::InstanceArray3D)
