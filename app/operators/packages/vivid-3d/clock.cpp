#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/value_view.h"
#include "operator_api/metronome_sync.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// =============================================================================
// Clock — a transport-synced TIMING SOURCE for the visual graph
// =============================================================================
//
// Emits three single-value lanes so downstream nodes can lock to the musical clock:
//   step  — a monotonic (or wrapped 0..steps-1) tick INDEX, advancing once per `period`
//   phase — 0..1 progress THROUGH the current tick
//   gate  — a brief 1.0 pulse at the very start of each tick (else 0)
//
// Composable, single-purpose: one Clock can drive many consumers in sync. Wire `step` into Switch3D to
// cut between looks every N bars; `phase` into a lane consumer for a tempo-locked ramp; `gate` to trigger
// a flash. Timing lives HERE — selection/animation live in the consumers.

struct Clock : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName        = "Clock";
    static constexpr const char* kSummary =
        "Transport-synced timing source: emits step / phase / gate lanes that advance once per `period` "
        "(e.g. every 4 bars). Wire `step` into Switch3D to cut between looks on the beat.";
    static constexpr bool kTimeDependent = true;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_GENERATE;

    vivid::Param<int>   sync   {"sync", 1, {"internal", "metronome"}};   // metronome = lock to the transport
    vivid::Param<int>   unit   {"unit", 1, {"beats", "bars"}};           // `period` is measured in…
    vivid::Param<float> period {"period", 4.0f, 0.0625f, 64.f};          // …this many units per tick (default 4 bars)
    vivid::Param<float> rate   {"rate", 0.5f, 0.001f, 20.f};             // ticks/sec when sync = internal
    vivid::Param<int>   steps  {"steps", 0, 0, 256};                     // 0 = free/monotonic; N = wrap step 0..N-1
    vivid::Param<float> gate_w {"gate_width", 0.08f, 0.001f, 1.f};       // gate stays high for this fraction of a tick

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&sync); out.push_back(&unit); out.push_back(&period);
        out.push_back(&rate); out.push_back(&steps); out.push_back(&gate_w);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        auto lane_out = [&](const char* name) {
            VividPortDescriptor p{};
            p.name = name; p.type = VIVID_PORT_SCALAR;
            p.direction = VIVID_PORT_OUTPUT; p.multiplicity = VIVID_MULTIPLICITY_MANY;
            p.semantic_shape = "scalar";
            out.push_back(p);
        };
        lane_out("step");    // 0
        lane_out("phase");   // 1
        lane_out("gate");    // 2
    }

    void draw_thumbnail(const VividThumbnailContext*) override {}

    void process_gpu(const VividGpuContext* ctx) override {
        // Continuous tick position: integer part = which tick, fractional part = phase within it.
        double tickf;
        if (sync.int_value() == 0) {                        // internal: ticks/sec off the wall clock
            tickf = ctx->time * static_cast<double>(std::max(1e-4f, rate.value));
        } else {                                            // metronome: lock to the transport
            const vivid::MetronomeTransport tr = vivid::metronome_transport(ctx);
            const double bpb = tr.beats_per_bar > 0 ? static_cast<double>(tr.beats_per_bar) : 4.0;
            const double per_beats = std::max(1e-4, static_cast<double>(period.value) *
                                               (unit.int_value() == 1 ? bpb : 1.0));
            tickf = tr.beats_elapsed / per_beats;
        }
        if (tickf < 0.0) tickf = 0.0;

        double stepd      = std::floor(tickf);
        const float phase = static_cast<float>(tickf - stepd);
        const int   nstep = steps.int_value();
        if (nstep > 0) stepd = std::fmod(stepd, static_cast<double>(nstep));
        const float gate  = (phase < std::clamp(gate_w.value, 0.001f, 1.f)) ? 1.f : 0.f;

        emit(ctx, 0, static_cast<float>(stepd));
        emit(ctx, 1, phase);
        emit(ctx, 2, gate);
    }

private:
    static void emit(const VividGpuContext* ctx, int port, float v) {
        if (!ctx->value_outputs) return;
        if (float* buf = vivid_value_output_floats(&ctx->value_outputs[port], 1)) {
            buf[0] = v;
            vivid_value_output_commit(&ctx->value_outputs[port], 1);
        }
    }
};

VIVID_REGISTER(Clock)
VIVID_THUMBNAIL(Clock)
