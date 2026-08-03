#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/value_view.h"
#include "operator_api/metronome_sync.h"
#include "operator_api/lane_thumb.h"   // 2D thumbnail: a live clock face
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

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
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;   // ADR-0046
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

        // Live clock face: a dim disc + a pie wedge that fills with `phase` each tick and flashes on the
        // gate, so the node thumbnail visibly TICKS in the graph (and you can read the tempo at a glance).
        draw_face(ctx, phase, gate, static_cast<float>(stepd));
    }

    ~Clock() override { vivid::lanethumb::destroy(thumb_); }

private:
    vivid::lanethumb::State thumb_{};

    static void emit(const VividGpuContext* ctx, int port, float v) {
        if (!ctx->value_outputs) return;
        if (float* buf = vivid_value_output_floats(&ctx->value_outputs[port], 1)) {
            buf[0] = v;
            vivid_value_output_commit(&ctx->value_outputs[port], 1);
        }
    }

    // Append a clockwise pie fan (fraction f0..f1 of a full turn, from 12 o'clock) to `v`.
    static void pie(std::vector<vivid::lanethumb::Vtx>& v, float R, float sx, float sy,
                    const float col[3], float f0, float f1) {
        const int seg = std::max(1, static_cast<int>(std::ceil((f1 - f0) * 48.f)));
        const float TAU = 6.2831853f;
        for (int i = 0; i < seg; ++i) {
            const float a0 = (f0 + (f1 - f0) * (static_cast<float>(i)     / seg)) * TAU;
            const float a1 = (f0 + (f1 - f0) * (static_cast<float>(i + 1) / seg)) * TAU;
            const vivid::lanethumb::Vtx c  {{0.f, 0.f},                       {col[0], col[1], col[2]}};
            const vivid::lanethumb::Vtx p0 {{std::sin(a0) * R * sx, std::cos(a0) * R * sy}, {col[0], col[1], col[2]}};
            const vivid::lanethumb::Vtx p1 {{std::sin(a1) * R * sx, std::cos(a1) * R * sy}, {col[0], col[1], col[2]}};
            v.push_back(c); v.push_back(p0); v.push_back(p1);
        }
    }

    void draw_face(const VividGpuContext* ctx, float phase, float gate, float step) {
        if (!vivid::lanethumb::ok(ctx)) return;
        const float aspect = (ctx->output_height > 0)
                           ? static_cast<float>(ctx->output_width) / static_cast<float>(ctx->output_height) : 1.f;
        const float sx = aspect > 1.f ? 1.f / aspect : 1.f;    // keep the face circular whatever the aspect
        const float sy = aspect < 1.f ? aspect : 1.f;
        const float R  = 0.82f;

        std::vector<vivid::lanethumb::Vtx> v;
        const float dim[3] = { 0.16f, 0.18f, 0.24f };
        pie(v, R, sx, sy, dim, 0.f, 1.f);                       // full dim disc (the clock face)

        // Elapsed wedge: a cool accent that whitens on the gate pulse (the tick). Hue drifts a touch per
        // step so successive ticks read as counting.
        const float g   = std::clamp(gate, 0.f, 1.f);
        const float hue = 0.55f + 0.06f * std::sin(step * 1.3f);
        const float acc[3] = { std::clamp(0.25f + 0.75f * g + 0.15f * hue, 0.f, 1.f),
                               std::clamp(0.60f + 0.40f * g,               0.f, 1.f),
                               std::clamp(0.95f - 0.10f * hue + 0.05f * g, 0.f, 1.f) };
        pie(v, R * 0.94f, sx, sy, acc, 0.f, std::max(0.015f, phase));   // filling wedge = phase

        const float hub[3] = { 0.9f, 0.95f, 1.0f };             // bright hub so 12 o'clock reads
        pie(v, R * 0.14f, sx, sy, hub, 0.f, 1.f);

        vivid::lanethumb::draw(ctx, thumb_, v);
    }
};

VIVID_REGISTER(Clock)
VIVID_THUMBNAIL(Clock)
