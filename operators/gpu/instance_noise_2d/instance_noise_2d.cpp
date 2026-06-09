#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_2d.h"
#include <cmath>
#include <cstdint>
#include <vector>

// =============================================================================
// InstanceNoise2D — perturb an InstanceArray2D with time-varying noise
// =============================================================================

/**
 * @brief Add time-varying noise to per-instance position, rotation, and scale.
 *
 * 2D analog of InstanceNoise (3D). Takes an InstanceArray2D input and emits a
 * modified bundle where each instance's mat3x2 transform is perturbed by
 * independent smooth value noise. Useful as a modifier between a layout
 * generator (InstanceGrid2D) and a consumer (Instancer2D).
 *
 * Each instance evolves with a golden-ratio phase offset so motion reads as
 * organic rather than synchronised.
 *
 * Transform composition per instance: given the input transform M with
 * linear block L (2×2) and translation t, the jittered transform is
 *     M' = [R(θ_n) · (1 + s_n) · L  |  t + p_n]
 * so rotation acts around each instance's own origin and position offsets
 * are applied in the output (world) frame.
 *
 * @param position_jitter World-space position jitter amplitude (NDC).
 * @param rotation_jitter Rotation jitter in radians.
 * @param scale_jitter    Scale variation (added to 1.0, clamped ≥ 0.05).
 * @param speed           Time multiplier for the noise animation.
 * @param seed            Decorrelation seed per-instance.
 *
 * @tip Wire between InstanceGrid2D and Instancer2D to add organic motion to a static layout.
 * @tip Rotation jitter acts around each instance's own center, not the world origin.
 * @recipe InstanceGrid2D -> InstanceNoise2D -> Instancer2D ← Shape2D -> Render2D
 * @pitfall No input = empty output. Always connect the `instances` input.
 * @common_companions InstanceGrid2D, Instancer2D, InstancesFromLanes2D
 * @best_used_with Instancer2D
 * @family 2D drawable pipeline
 * @see InstanceGrid2D, Instancer2D
 */
struct InstanceNoise2D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName               = "InstanceNoise2D";
    static constexpr bool kTimeDependent             = true;

    vivid::Param<float> position_jitter {"position_jitter", 0.05f, 0.0f, 2.0f};
    vivid::Param<float> rotation_jitter {"rotation_jitter", 0.0f,  0.0f, 6.2832f};
    vivid::Param<float> scale_jitter    {"scale_jitter",    0.0f,  0.0f, 2.0f};
    vivid::Param<float> speed           {"speed",           1.0f,  0.0f, 20.0f};
    vivid::Param<int>   seed            {"seed",            42,    0,    99999};

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
                                            vivid::gpu::InstanceArray2D));
        out.push_back(VIVID_CUSTOM_REF_PORT("instances", VIVID_PORT_OUTPUT,
                                            vivid::gpu::InstanceArray2D));
    }

    void process_gpu(const VividGpuContext* ctx) override {
        const vivid::gpu::InstanceArray2D* in = nullptr;
        if (ctx->custom_input_count > 0 && ctx->custom_inputs && ctx->custom_inputs[0]) {
            in = static_cast<const vivid::gpu::InstanceArray2D*>(ctx->custom_inputs[0]);
        }
        if (!in || !in->data || in->count == 0) {
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

            float* t = instances_[i].transform;  // [a, c, b, d, tx, ty]

            // Scale jitter — multiply the 2×2 linear block uniformly (isotropic).
            if (sj > 0.0f) {
                float sv = value_noise(phase + 29.3f, i * 7919u + s + 5u);
                float mul = 1.0f + sj * (sv * 2.0f - 1.0f);
                if (mul < 0.05f) mul = 0.05f;
                t[0] *= mul; t[1] *= mul;
                t[2] *= mul; t[3] *= mul;
            }

            // Rotation jitter — pre-multiply the 2×2 linear block by R(θ_n).
            // R * L where L columns are (t[0], t[1]) and (t[2], t[3]).
            if (rj > 0.0f) {
                float theta = rj * (value_noise(phase + 11.5f, i * 7919u + s + 3u) * 2.0f - 1.0f);
                float cr = std::cos(theta);
                float sr = std::sin(theta);
                float a = t[0], c = t[1], b = t[2], d = t[3];
                t[0] = cr * a - sr * c;  // new a
                t[1] = sr * a + cr * c;  // new c
                t[2] = cr * b - sr * d;  // new b
                t[3] = sr * b + cr * d;  // new d
            }

            // Position jitter — add to the translation column directly.
            if (pj > 0.0f) {
                t[4] += pj * (value_noise(phase,          i * 7919u + s)      * 2.0f - 1.0f);
                t[5] += pj * (value_noise(phase + 37.1f,  i * 7919u + s + 1u) * 2.0f - 1.0f);
            }
        }

        bundle_.data  = instances_.data();
        bundle_.count = n;
        ctx->custom_outputs[0] = &bundle_;
    }

private:
    std::vector<vivid::gpu::InstanceData2D> instances_;
    vivid::gpu::InstanceArray2D bundle_{};
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

VIVID_DEFINE_OP(InstanceNoise2D) {
}

