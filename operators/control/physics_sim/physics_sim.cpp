#include "operator_api/operator.h"
#include <cmath>
#include <cstdint>

// =============================================================================
// PhysicsSim — N-body particle simulation with spread outputs
// =============================================================================

struct PhysicsSim : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "PhysicsSim";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   count      {"count",      8,     1,    16};
    vivid::Param<float> speed      {"speed",      1.0f,  0.0f, 5.0f};
    vivid::Param<int>   bounce_mode{"bounce_mode",1,     0,    1};
    vivid::Param<float> gravity    {"gravity",    0.0f, -20.0f, 20.0f};
    vivid::Param<float> damping    {"damping",    0.99f, 0.9f, 1.0f};
    vivid::Param<float> repulsion  {"repulsion",  0.3f,  0.0f, 1.0f};
    vivid::Param<float> min_radius {"min_radius", 0.05f, 0.02f, 0.2f};
    vivid::Param<float> max_radius {"max_radius", 0.15f, 0.05f, 0.3f};

    PhysicsSim() {
        static const char* bounce_labels[] = {"Wrap", "Bounce"};
        bounce_mode.choice_labels = bounce_labels;
        bounce_mode.choice_count  = 2;
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&count);
        out.push_back(&speed);
        out.push_back(&bounce_mode);
        out.push_back(&gravity);
        out.push_back(&damping);
        out.push_back(&repulsion);
        out.push_back(&min_radius);
        out.push_back(&max_radius);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"force",  VIVID_PORT_FLOAT,  VIVID_PORT_INPUT});   // float input 0
        out.push_back({"energy", VIVID_PORT_FLOAT,  VIVID_PORT_INPUT});   // float input 1
        out.push_back({"pos_x",  VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});  // spread output 0
        out.push_back({"pos_y",  VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});  // spread output 1
        out.push_back({"radii",  VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});  // spread output 2
    }

    void process(const VividProcessContext* ctx) override {
        float dt = static_cast<float>(ctx->delta_time) * ctx->param_values[1]; // * speed
        uint32_t n = static_cast<uint32_t>(ctx->param_values[0]); // count
        if (n < 1) n = 1;
        if (n > 16) n = 16;

        int bounce = static_cast<int>(ctx->param_values[2]);    // bounce_mode
        float grav = ctx->param_values[3];                       // gravity
        float damp = ctx->param_values[4];                       // damping
        float rep  = ctx->param_values[5];                       // repulsion
        float r_min = ctx->param_values[6];                      // min_radius
        float r_max = ctx->param_values[7];                      // max_radius

        // Read float inputs
        float force_input  = (ctx->input_values) ? ctx->input_values[0] : 0.0f;
        float energy_raw   = (ctx->input_values) ? ctx->input_values[1] : 0.0f;
        float energy_input = (energy_raw > 0.001f) ? energy_raw : 1.0f;

        // Initialize particles if count changed or first run
        if (!initialized_ || n != prev_count_) {
            init_particles(n, r_min, r_max);
            prev_count_ = n;
            initialized_ = true;
        }

        // Update velocities with forces
        for (uint32_t i = 0; i < n; ++i) {
            // Gravity
            vy_[i] += grav * dt;

            // External force (audio-reactive): adds random-ish impulse
            if (force_input > 0.01f) {
                float angle = hash_float(static_cast<uint32_t>(frame_ * 7 + i * 31)) * 6.2831853f;
                float mag = force_input * dt * 2.0f;
                vx_[i] += std::cos(angle) * mag;
                vy_[i] += std::sin(angle) * mag;
            }

            // Energy scales velocity magnitude
            vx_[i] *= std::pow(damp, dt * 60.0f) * (0.5f + energy_input * 0.5f);
            vy_[i] *= std::pow(damp, dt * 60.0f) * (0.5f + energy_input * 0.5f);
        }

        // Pairwise repulsion
        for (uint32_t i = 0; i < n; ++i) {
            for (uint32_t j = i + 1; j < n; ++j) {
                float dx = px_[i] - px_[j];
                float dy = py_[i] - py_[j];
                float dist = std::sqrt(dx * dx + dy * dy);
                float min_dist = radii_[i] + radii_[j];

                if (dist < min_dist && dist > 0.001f) {
                    float overlap = min_dist - dist;
                    float nx = dx / dist;
                    float ny = dy / dist;
                    float push = rep * overlap * 5.0f * dt;

                    vx_[i] += nx * push;
                    vy_[i] += ny * push;
                    vx_[j] -= nx * push;
                    vy_[j] -= ny * push;
                }
            }
        }

        // Integrate positions
        for (uint32_t i = 0; i < n; ++i) {
            px_[i] += vx_[i] * dt;
            py_[i] += vy_[i] * dt;

            if (bounce == 0) {
                // Wrap
                px_[i] = std::fmod(px_[i] + 10.0f, 1.0f);
                py_[i] = std::fmod(py_[i] + 10.0f, 1.0f);
            } else {
                // Bounce
                float ri = radii_[i] * 0.5f;
                if (px_[i] < ri) { px_[i] = ri; vx_[i] = std::abs(vx_[i]); }
                if (px_[i] > 1.0f - ri) { px_[i] = 1.0f - ri; vx_[i] = -std::abs(vx_[i]); }
                if (py_[i] < ri) { py_[i] = ri; vy_[i] = std::abs(vy_[i]); }
                if (py_[i] > 1.0f - ri) { py_[i] = 1.0f - ri; vy_[i] = -std::abs(vy_[i]); }
            }
        }

        // Write spread outputs
        if (ctx->output_spreads) {
            auto& sp_x = ctx->output_spreads[0];
            if (sp_x.capacity >= n) {
                sp_x.length = n;
                for (uint32_t i = 0; i < n; ++i)
                    sp_x.data[i] = px_[i];
            }

            auto& sp_y = ctx->output_spreads[1];
            if (sp_y.capacity >= n) {
                sp_y.length = n;
                for (uint32_t i = 0; i < n; ++i)
                    sp_y.data[i] = py_[i];
            }

            auto& sp_r = ctx->output_spreads[2];
            if (sp_r.capacity >= n) {
                sp_r.length = n;
                for (uint32_t i = 0; i < n; ++i)
                    sp_r.data[i] = radii_[i];
            }
        }

        frame_++;
    }

private:
    float px_[16]{}, py_[16]{}, vx_[16]{}, vy_[16]{}, radii_[16]{};
    uint32_t prev_count_ = 0;
    uint64_t frame_ = 0;
    bool initialized_ = false;

    void init_particles(uint32_t n, float r_min, float r_max) {
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t seed = i * 1337 + 42;
            px_[i] = hash_float(seed);
            py_[i] = hash_float(seed + 1);
            vx_[i] = (hash_float(seed + 2) - 0.5f) * 2.0f;
            vy_[i] = (hash_float(seed + 3) - 0.5f) * 2.0f;
            radii_[i] = r_min + (r_max - r_min) * hash_float(seed + 4);
        }
    }

    static float hash_float(uint32_t input) {
        uint32_t state = input * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        uint32_t result = (word >> 22u) ^ word;
        return static_cast<float>(result) / 4294967295.0f;
    }
};

VIVID_REGISTER(PhysicsSim)
