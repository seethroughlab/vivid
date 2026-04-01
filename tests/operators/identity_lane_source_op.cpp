// Test operator: structural spread source with identity-bearing lane_ids.
//
// Emits a controllable number of lanes with stable lane_ids allocated via
// allocate_lane_id_fn. The `active_mask` param controls which voices are
// active (bitmask). When a voice is deactivated, the output spreads are
// compacted (no gaps) and the retired lane_id is cleaned up.
//
// Used to test identity compaction: downstream operators using
// vivid_lane_state() keyed by lane_id must retain correct state when
// the spread compacts and lane order changes.

#include "operator_api/operator.h"
#include <vector>

struct IdentityLaneSourceOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "IdentityLaneSourceOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;

    // Max voices this source can emit.
    static constexpr uint32_t kMaxVoices = 8;

    // active_mask: bitmask of which voices are active (default: 0xF = first 4)
    vivid::Param<int> active_mask{"active_mask", 0xF, 0, 255};
    // base: multiplier for spread values (voice i outputs base * (i+1))
    vivid::Param<float> base{"base", 1.0f, 0.0f, 100.0f};

    // Fixed lane IDs per voice slot (deterministic, non-overlapping).
    // Using 100+i to avoid collision with positional IDs (1..N).
    static constexpr uint32_t kBaseLaneId = 100;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&active_mask);
        out.push_back(&base);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out",      VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"lane_ids", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        int mask = static_cast<int>(ctx->param_values[0]);
        float b  = ctx->param_values[1];

        // Count active voices and build compacted output
        uint32_t active_count = 0;
        float values[kMaxVoices];
        float ids[kMaxVoices];

        for (uint32_t i = 0; i < kMaxVoices; ++i) {
            if (mask & (1 << i)) {
                values[active_count] = b * static_cast<float>(i + 1);
                ids[active_count] = static_cast<float>(kBaseLaneId + i);
                active_count++;
            }
        }

        // Write scalar output (first active value or 0)
        ctx->output_values[0] = active_count > 0 ? values[0] : 0.0f;

        // Write main spread (out port, index 0)
        if (ctx->output_lanes) {
            auto& osp = ctx->output_lanes[0];
            if (osp.capacity >= active_count) {
                osp.length = active_count;
                for (uint32_t i = 0; i < active_count; ++i)
                    osp.data[i] = values[i];
            }

            // Write lane_ids spread (lane_ids port, index 1)
            auto& lid_sp = ctx->output_lanes[1];
            if (lid_sp.capacity >= active_count) {
                lid_sp.length = active_count;
                for (uint32_t i = 0; i < active_count; ++i)
                    lid_sp.data[i] = ids[i];
            }
        }
    }
};

VIVID_REGISTER(IdentityLaneSourceOp)
