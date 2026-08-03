#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include "operator_api/value_view.h"
#include "operator_api/lane_thumb.h"   // 2D node thumbnail: a row of input slots, the selected one lit
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// =============================================================================
// Switch3D — forward ONE of several Scene3D inputs, chosen by a Clock's `step`
// =============================================================================
//
// The selection half of the quantized-cut pair (timing lives in Clock — composable, not monolithic).
// Wire N look-variants (each its own Shape3D/Deformer/material subgraph) into scene_a..scene_f, wire a
// Clock's `step` output into the `clock` input, and Switch3D forwards input[step] — cutting between
// entirely different looks (geometry + material + colour) on the Clock's schedule. `order` picks how the
// step maps to an input: sequential, ping-pong, or a stable per-tick shuffle. With no clock wired it
// forwards the first connected input (static passthrough).

struct Switch3D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Switch3D";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_TRANSFORM;   // ADR-0046
    static constexpr const char* kSummary =
        "Forwards one of several Scene3D inputs, selected by a Clock's `step` (wire it into `clock`). "
        "Cut between whole looks every N bars — timing comes from the Clock node.";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int> order {"order", 0, {"sequential", "pingpong", "random"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&order);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // `clock` is the FIRST input port (ordinal 0) so it lands at ctx->values[0]. Scene inputs are
        // custom-ref and pack into custom_inputs[0..] in declared order (scene_a..scene_f).
        VividPortDescriptor c{};
        c.name = "clock"; c.type = VIVID_PORT_SCALAR;
        c.direction = VIVID_PORT_INPUT; c.multiplicity = VIVID_MULTIPLICITY_MANY;
        c.semantic_shape = "scalar";
        out.push_back(c);
        out.push_back(vivid::gpu::scene_port("scene_a", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::scene_port("scene_b", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::scene_port("scene_c", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::scene_port("scene_d", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::scene_port("scene_e", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::scene_port("scene_f", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::scene_port("scene",   VIVID_PORT_OUTPUT));
    }

    void draw_thumbnail(const VividThumbnailContext*) override {}

    void process_gpu(const VividGpuContext* ctx) override {
        // Collect the connected scene inputs (custom_inputs is packed: scene_a..scene_f, nulls skipped).
        vivid::gpu::VividSceneFragment* ins[6]{};
        int n = 0;
        for (uint32_t i = 0; i < ctx->custom_input_count && n < 6; ++i) {
            if (auto* s = vivid::gpu::scene_input(ctx, i)) ins[n++] = s;
        }

        // Clock step from value input port 0 (0 if unwired → static, forwards the first input).
        float stepf = 0.f;
        if (ctx->values && vivid_value_count(&ctx->values[0]) > 0) {
            if (const float* c = vivid_value_floats(&ctx->values[0])) stepf = c[0];
        }
        long step = static_cast<long>(std::floor(static_cast<double>(stepf)));
        if (step < 0) step = 0;

        int idx = 0;
        if (n > 0) {
            const int ord = order.int_value();
            if (ord == 1 && n > 1) {                   // pingpong: 0,1,..,n-1,n-2,..,1, repeat
                const int span = 2 * (n - 1);
                const int m = static_cast<int>(step % span);
                idx = (m < n) ? m : (span - m);
            } else if (ord == 2) {                     // random: stable per-tick hash (no gaps, reproducible)
                uint32_t h = static_cast<uint32_t>(step) * 2654435761u + 1013904223u;
                h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
                idx = static_cast<int>(h % static_cast<uint32_t>(n));
            } else {                                   // sequential
                idx = static_cast<int>(step % n);
            }
            ctx->custom_outputs[0] = ins[idx];         // forward the selected fragment unchanged
        }

        render_thumb(ctx, n, idx);                     // a row of input slots, the live selection lit
    }

    ~Switch3D() override { vivid::lanethumb::destroy(thumb_); }

private:
    vivid::lanethumb::State thumb_{};

    // Node-card thumbnail: a row of slots (one per connected input, ≥3 when idle) with the currently
    // selected slot lit — so the card visibly shows what the Switch is doing as the clock advances.
    void render_thumb(const VividGpuContext* ctx, int n, int idx) {
        if (!vivid::lanethumb::ok(ctx)) return;
        const int cells = std::max(3, n);
        const float pad = 0.10f, span = 2.f - 2.f * pad;
        const float gap = span / cells * 0.22f, w = span / cells - gap;
        const float on[3]  = { 0.35f, 0.68f, 1.00f };   // selected slot
        const float off[3] = { 0.18f, 0.20f, 0.26f };   // idle slot
        std::vector<vivid::lanethumb::Vtx> v;
        for (int i = 0; i < cells; ++i) {
            const float x0 = -1.f + pad + i * (span / cells);
            const float* c = (n > 0 && i == idx) ? on : off;
            vivid::lanethumb::quad(v, x0, -0.55f, x0 + w, 0.55f, c);
        }
        vivid::lanethumb::draw(ctx, thumb_, v);
    }
};

VIVID_REGISTER(Switch3D)
VIVID_THUMBNAIL(Switch3D)
