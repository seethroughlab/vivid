#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/reactive_bus.h"   // host: vivid_master_signals (resolved at dlopen)
#include "operator_api/reactive_signals.h"   // canonical signal names (== value-lane ordinal == bridge suffix)
#include "operator_api/value_view.h"     // FLOAT-MANY (count-1) lane outputs
#include "operator_api/lane_thumb.h"     // 2D bar-chart node thumbnail
#include <algorithm>
#include <cstdint>
#include <vector>

// =============================================================================
// ReactiveMaster — the live master audio characteristics as wireable value outputs
// =============================================================================
//
// ADR-0053 Phase B: the visible, first-class SOURCE node that replaces the hidden "master.*" bridge
// mapping. It reads the reactive bus (level/transient/low/mid/high + transport beat/bar_phase/downbeat/
// beat_pulse) and emits ONE count-1 value lane per signal. Wire any output into any visual op parameter
// (via a control edge) so audio->visual reactivity is a REAL edge on the canvas, not a string keyed into
// a registry. Pure analysis: no params, no texture I/O — the signals are already conditioned host-side.
struct ReactiveMaster : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "ReactiveMaster";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;   // ADR-0046
    static constexpr bool kTimeDependent = true;   // reads live audio every frame

    void collect_params(std::vector<vivid::ParamBase*>&) override {}   // pure source, no params

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        for (const char* name : vivid::reactive::kMasterSignals) {
            VividPortDescriptor p{};
            p.name = name; p.type = VIVID_PORT_SCALAR;
            p.direction = VIVID_PORT_OUTPUT; p.multiplicity = VIVID_MULTIPLICITY_MANY;
            p.semantic_shape = "signal";
            out.push_back(p);
        }
    }

    void draw_thumbnail(const VividThumbnailContext*) override {}

    void process_gpu(const VividGpuContext* ctx) override {
        float sig[VIVID_REACTIVE_MASTER_SIGNALS] = {0.f};
        const uint32_t got = vivid_master_signals(sig, VIVID_REACTIVE_MASTER_SIGNALS);
        (void)got;   // absent audio -> all zero -> consumers fall back to their param base

        // Publish each signal as its own count-1 value lane (one output port per signal).
        if (ctx->value_outputs) {
            for (uint32_t i = 0; i < VIVID_REACTIVE_MASTER_SIGNALS; ++i) {
                if (float* buf = vivid_value_output_floats(&ctx->value_outputs[i], 1)) {
                    buf[0] = sig[i];
                    vivid_value_output_commit(&ctx->value_outputs[i], 1);
                }
            }
        }
        // Node thumbnail: a live mini bar chart of all master signals.
        const float col[3] = { 1.0f, 0.55f, 0.2f };   // teal-warm accent (source node)
        vivid::lanethumb::render_bars(ctx, thumb_, sig, VIVID_REACTIVE_MASTER_SIGNALS, col);
    }

    ~ReactiveMaster() override { vivid::lanethumb::destroy(thumb_); }

private:
    vivid::lanethumb::State thumb_{};
};

VIVID_REGISTER(ReactiveMaster)
VIVID_THUMBNAIL(ReactiveMaster)
