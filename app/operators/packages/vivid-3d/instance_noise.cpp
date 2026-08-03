#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include "operator_api/thumbnail_3d.h"
#include <cmath>
#include <cstdint>
#include <vector>

// =============================================================================
// InstanceNoise — perturb an InstanceArray3D with time-varying noise
// =============================================================================

/**
 * @brief Add time-varying noise to per-instance position, rotation, and scale.
 *
 * Takes an InstanceArray3D input and emits a modified bundle where each
 * instance is perturbed by independent smooth value noise. Useful as a
 * modifier between a layout generator (InstanceGrid) and a consumer
 * (Instancer3D). Each instance evolves with decorrelated phase so the
 * motion reads as organic rather than synchronised.
 *
 * @param position_jitter World-space position jitter amplitude.
 * @param rotation_jitter Rotation jitter in radians.
 * @param scale_jitter    Scale variation (added to 1.0, clamped ≥ 0.05).
 * @param speed           Time multiplier for the noise animation.
 * @param seed            Decorrelation seed per-instance.
 */
struct InstanceNoise : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName         = "InstanceNoise";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_TRANSFORM;   // ADR-0046
    static constexpr bool kTimeDependent       = true;
    // POINTWISE → Map is the default multiplicity behavior; no explicit declaration needed.

    vivid::Param<float> position_jitter {"position_jitter", 0.0f, 0.0f, 10.0f};
    vivid::Param<float> rotation_jitter {"rotation_jitter", 0.0f, 0.0f, 6.2832f};
    vivid::Param<float> scale_jitter    {"scale_jitter",    0.0f, 0.0f, 2.0f};
    vivid::Param<float> speed           {"speed",           1.0f, 0.0f, 20.0f};
    vivid::Param<int>   seed            {"seed",            42,   0,    99999};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(position_jitter, "Jitter");
        vivid::param_group(rotation_jitter, "Jitter");
        vivid::param_group(scale_jitter,    "Jitter");
        vivid::param_group(speed,           "Animation");
        vivid::param_group(seed,            "Animation");
        vivid::semantic_tag(seed, "seed");
        vivid::semantic_shape(seed, "int");
        out.push_back(&position_jitter);
        out.push_back(&rotation_jitter);
        out.push_back(&scale_jitter);
        out.push_back(&speed);
        out.push_back(&seed);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("instances", VIVID_PORT_INPUT,
                                            vivid::gpu::InstanceArray3D));
        out.push_back(VIVID_CUSTOM_REF_PORT("instances", VIVID_PORT_OUTPUT,
                                            vivid::gpu::InstanceArray3D));
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override { /* TODO(ADR-0041 Phase 1): reimplement vs trunk 2D VividDrawAPI */ }

    void process_gpu(const VividGpuContext* ctx) override {
        const vivid::gpu::InstanceArray3D* in = nullptr;
        if (ctx->custom_input_count > 0 && ctx->custom_inputs && ctx->custom_inputs[0]) {
            in = static_cast<const vivid::gpu::InstanceArray3D*>(ctx->custom_inputs[0]);
        }
        if (!in || !in->data || in->count == 0) {
            // No input: emit an empty bundle.
            instances_.clear();
            bundle_.data  = nullptr;
            bundle_.count = 0;
            ctx->custom_outputs[0] = &bundle_;
            return;
        }

        uint32_t n = in->count;
        instances_.resize(n);

        time_ += static_cast<float>(ctx->delta_time) * speed.value;

        const float pj = position_jitter.value;
        const float rj = rotation_jitter.value;
        const float sj = scale_jitter.value;
        const uint32_t s = static_cast<uint32_t>(seed.int_value());

        for (uint32_t i = 0; i < n; ++i) {
            instances_[i] = in->data[i];

            // Golden-ratio phase offset per instance for decorrelation.
            float phase = time_ + static_cast<float>(i) * 0.618033988749895f;

            if (pj > 0.0f) {
                instances_[i].position[0] += pj * (value_noise(phase,            i * 7919u + s) * 2.0f - 1.0f);
                instances_[i].position[1] += pj * (value_noise(phase + 37.1f,    i * 7919u + s + 1u) * 2.0f - 1.0f);
                instances_[i].position[2] += pj * (value_noise(phase + 71.3f,    i * 7919u + s + 2u) * 2.0f - 1.0f);
            }
            if (rj > 0.0f) {
                instances_[i].rotation_y += rj * (value_noise(phase + 11.5f,  i * 7919u + s + 3u) * 2.0f - 1.0f);
                instances_[i].rotation_x += rj * (value_noise(phase + 19.7f,  i * 7919u + s + 4u) * 2.0f - 1.0f);
            }
            if (sj > 0.0f) {
                float sv0 = value_noise(phase + 29.3f, i * 7919u + s + 5u);
                float sv1 = value_noise(phase + 41.9f, i * 7919u + s + 6u);
                float sv2 = value_noise(phase + 53.7f, i * 7919u + s + 7u);
                float mx = 1.0f + sj * (sv0 * 2.0f - 1.0f);
                float my = 1.0f + sj * (sv1 * 2.0f - 1.0f);
                float mz = 1.0f + sj * (sv2 * 2.0f - 1.0f);
                if (mx < 0.05f) mx = 0.05f;
                if (my < 0.05f) my = 0.05f;
                if (mz < 0.05f) mz = 0.05f;
                instances_[i].scale[0] *= mx;
                instances_[i].scale[1] *= my;
                instances_[i].scale[2] *= mz;
            }
        }

        bundle_.data  = instances_.data();
        bundle_.count = n;
        ctx->custom_outputs[0] = &bundle_;

        // Animated 3D thumbnail: a unit cube instanced at the jittered transforms.
        vivid::thumb3d::render_instances_cpu(ctx, thumb_, instances_.data(), n);
    }

    ~InstanceNoise() override { vivid::thumb3d::destroy(thumb_); }

private:
    std::vector<vivid::gpu::InstanceData3D> instances_;
    vivid::gpu::InstanceArray3D bundle_{};
    vivid::thumb3d::State thumb_{};
    float time_ = 0.0f;

    // Smooth value noise: hash at integer t, smoothstep interpolate.
    static float value_noise(float t, uint32_t extra_seed) {
        float tf = std::floor(t);
        float frac = t - tf;
        int32_t t0 = static_cast<int32_t>(tf);
        int32_t t1 = t0 + 1;
        float v0 = hash_float(static_cast<uint32_t>(t0) + extra_seed);
        float v1 = hash_float(static_cast<uint32_t>(t1) + extra_seed);
        float smooth = frac * frac * (3.0f - 2.0f * frac);
        return v0 + (v1 - v0) * smooth;
    }

    static float hash_float(uint32_t input) {
        uint32_t state  = input * 747796405u + 2891336453u;
        uint32_t word   = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        uint32_t result = (word >> 22u) ^ word;
        return static_cast<float>(result) / 4294967295.0f;
    }
};

VIVID_REGISTER(InstanceNoise)
VIVID_THUMBNAIL(InstanceNoise)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::InstanceArray3D)
