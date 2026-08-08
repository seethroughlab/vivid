#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/reactive_bus.h"   // host: vivid_track_signals (resolved at dlopen)
#include "operator_api/reactive_signals.h"   // canonical signal names (== value-lane ordinal == bridge suffix)
#include "operator_api/value_view.h"     // FLOAT-MANY (count-1) lane outputs
#include "operator_api/lane_thumb.h"     // 2D bar-chart node thumbnail
#include <algorithm>
#include <cstdint>
#include <vector>

// =============================================================================
// ReactiveTrack — one track's live audio characteristics as wireable value outputs
// =============================================================================
//
// ADR-0053 Phase B: the visible SOURCE node that replaces the hidden "track_<id>.*" bridge mapping. It
// binds a STABLE track id (the `track_id` param — the same id track_<id>.* sources use) and reads that
// track's signals off the reactive bus (level/transient/low/mid/high + note/velocity/gate), emitting ONE
// count-1 value lane per signal. Addressing by stable id means the node keeps following its track across
// reorder/delete, like every other visual source. Wire an output into a visual op parameter via a
// control edge.
struct ReactiveTrack : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "ReactiveTrack";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;   // ADR-0046
    static constexpr bool kTimeDependent = true;   // reads live audio every frame

    vivid::Param<int> track_id{"track_id", 0, 0, 4095};   // STABLE id of the track this node follows

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&track_id);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        for (const char* name : vivid::reactive::kTrackSignals) {
            VividPortDescriptor p{};
            p.name = name; p.type = VIVID_PORT_SCALAR;
            p.direction = VIVID_PORT_OUTPUT; p.multiplicity = VIVID_MULTIPLICITY_MANY;
            p.semantic_shape = "signal";
            out.push_back(p);
        }
    }

    void draw_thumbnail(const VividThumbnailContext*) override {}

    void process_gpu(const VividGpuContext* ctx) override {
        float sig[VIVID_REACTIVE_TRACK_SIGNALS] = {0.f};
        const uint32_t got = vivid_track_signals(track_id.int_value(), sig, VIVID_REACTIVE_TRACK_SIGNALS);
        (void)got;   // track not live -> all zero -> consumers fall back to their param base

        // Publish each signal as its own count-1 value lane (one output port per signal).
        if (ctx->value_outputs) {
            for (uint32_t i = 0; i < VIVID_REACTIVE_TRACK_SIGNALS; ++i) {
                if (float* buf = vivid_value_output_floats(&ctx->value_outputs[i], 1)) {
                    buf[0] = sig[i];
                    vivid_value_output_commit(&ctx->value_outputs[i], 1);
                }
            }
        }
        // Node thumbnail: a live mini bar chart of the track's signals.
        const float col[3] = { 0.4f, 0.85f, 1.0f };
        vivid::lanethumb::render_bars(ctx, thumb_, sig, VIVID_REACTIVE_TRACK_SIGNALS, col);
    }

    ~ReactiveTrack() override { vivid::lanethumb::destroy(thumb_); }

private:
    vivid::lanethumb::State thumb_{};
};

VIVID_REGISTER(ReactiveTrack)
VIVID_THUMBNAIL(ReactiveTrack)
