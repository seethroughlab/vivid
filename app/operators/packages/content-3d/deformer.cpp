#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include "operator_api/thumbnail_3d.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

// =============================================================================
// Simplex noise — open-domain implementation (public domain / MIT)
// =============================================================================

namespace {

static constexpr float F3 = 1.0f / 3.0f;
static constexpr float G3 = 1.0f / 6.0f;

// Gradient vectors for 3D simplex noise
static const int grad3[12][3] = {
    {1,1,0}, {-1,1,0}, {1,-1,0}, {-1,-1,0},
    {1,0,1}, {-1,0,1}, {1,0,-1}, {-1,0,-1},
    {0,1,1}, {0,-1,1}, {0,1,-1}, {0,-1,-1}
};

// Permutation table
static const uint8_t perm[512] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
    35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
    134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
    55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
    18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
    250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
    189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
    172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
    228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
    107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
    138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
    35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
    134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
    55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
    18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
    250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
    189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
    172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
    228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
    107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
    138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

static float dot3(const int g[3], float x, float y, float z) {
    return static_cast<float>(g[0]) * x + static_cast<float>(g[1]) * y + static_cast<float>(g[2]) * z;
}

static int fast_floor(float x) {
    int xi = static_cast<int>(x);
    return x < static_cast<float>(xi) ? xi - 1 : xi;
}

float simplex3d(float x, float y, float z) {
    float s = (x + y + z) * F3;
    int i = fast_floor(x + s);
    int j = fast_floor(y + s);
    int k = fast_floor(z + s);

    float t = static_cast<float>(i + j + k) * G3;
    float X0 = static_cast<float>(i) - t;
    float Y0 = static_cast<float>(j) - t;
    float Z0 = static_cast<float>(k) - t;
    float x0 = x - X0, y0 = y - Y0, z0 = z - Z0;

    int i1, j1, k1, i2, j2, k2;
    if (x0 >= y0) {
        if (y0 >= z0)      { i1=1; j1=0; k1=0; i2=1; j2=1; k2=0; }
        else if (x0 >= z0) { i1=1; j1=0; k1=0; i2=1; j2=0; k2=1; }
        else               { i1=0; j1=0; k1=1; i2=1; j2=0; k2=1; }
    } else {
        if (y0 < z0)       { i1=0; j1=0; k1=1; i2=0; j2=1; k2=1; }
        else if (x0 < z0)  { i1=0; j1=1; k1=0; i2=0; j2=1; k2=1; }
        else               { i1=0; j1=1; k1=0; i2=1; j2=1; k2=0; }
    }

    float x1 = x0 - static_cast<float>(i1) + G3;
    float y1 = y0 - static_cast<float>(j1) + G3;
    float z1 = z0 - static_cast<float>(k1) + G3;
    float x2 = x0 - static_cast<float>(i2) + 2.0f * G3;
    float y2 = y0 - static_cast<float>(j2) + 2.0f * G3;
    float z2 = z0 - static_cast<float>(k2) + 2.0f * G3;
    float x3 = x0 - 1.0f + 3.0f * G3;
    float y3 = y0 - 1.0f + 3.0f * G3;
    float z3 = z0 - 1.0f + 3.0f * G3;

    int ii = i & 255, jj = j & 255, kk = k & 255;
    int gi0 = perm[ii + perm[jj + perm[kk]]] % 12;
    int gi1 = perm[ii + i1 + perm[jj + j1 + perm[kk + k1]]] % 12;
    int gi2 = perm[ii + i2 + perm[jj + j2 + perm[kk + k2]]] % 12;
    int gi3 = perm[ii + 1 + perm[jj + 1 + perm[kk + 1]]] % 12;

    float n0 = 0, n1 = 0, n2 = 0, n3 = 0;
    float t0 = 0.6f - x0*x0 - y0*y0 - z0*z0;
    if (t0 >= 0) { t0 *= t0; n0 = t0 * t0 * dot3(grad3[gi0], x0, y0, z0); }
    float t1 = 0.6f - x1*x1 - y1*y1 - z1*z1;
    if (t1 >= 0) { t1 *= t1; n1 = t1 * t1 * dot3(grad3[gi1], x1, y1, z1); }
    float t2 = 0.6f - x2*x2 - y2*y2 - z2*z2;
    if (t2 >= 0) { t2 *= t2; n2 = t2 * t2 * dot3(grad3[gi2], x2, y2, z2); }
    float t3 = 0.6f - x3*x3 - y3*y3 - z3*z3;
    if (t3 >= 0) { t3 *= t3; n3 = t3 * t3 * dot3(grad3[gi3], x3, y3, z3); }

    return 32.0f * (n0 + n1 + n2 + n3);  // range roughly [-1, 1]
}

} // anonymous namespace

// =============================================================================
// Deformer Operator — CPU vertex displacement
// =============================================================================

/**
 * @brief Deforms incoming 3D geometry with procedural or audio-driven motion.
 *
 * Deformer offsets mesh vertices using noise, sine motion, or external modulation so shapes can
 * feel animated and reactive without rewriting the source mesh.
 *
 * @param mode Deformation mode.
 * @param amplitude Amount of geometric displacement.
 * @param frequency Spatial deformation frequency.
 * @param speed Animation speed for time-varying deformation.
 * @param axis Axis selection for the deformation.
 */
struct Deformer : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Deformer";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_TRANSFORM;   // ADR-0046
    static constexpr const char* kSummary = "Displaces a mesh's vertices with noise or a wave, so geometry ripples and breathes.";
    static constexpr std::array<const char*, 3> kKeywords = {"3d", "deform", "displace"};
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   mode      {"mode", 0, {"Noise", "Sine", "Audio"}};
    vivid::Param<float> amplitude {"amplitude", 0.3f, 0.0f, 5.0f};
    vivid::Param<float> frequency {"frequency", 3.0f, 0.01f, 50.0f};
    vivid::Param<float> speed     {"speed",     1.0f, 0.0f, 10.0f};
    vivid::Param<int>   axis      {"axis", 0, {"All", "X", "Y", "Z"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(mode, "Deformer");
        vivid::param_group(amplitude, "Deformer");
        vivid::param_group(frequency, "Deformer");
        vivid::param_group(speed, "Deformer");
        vivid::param_group(axis, "Deformer");

        out.push_back(&mode);
        out.push_back(&amplitude);
        out.push_back(&frequency);
        out.push_back(&speed);
        out.push_back(&axis);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::scene_port("scene", VIVID_PORT_INPUT));
        out.push_back({"amount", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back(vivid::gpu::scene_port("scene", VIVID_PORT_OUTPUT));
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override { /* TODO(ADR-0041 Phase 1): reimplement vs trunk 2D VividDrawAPI */ }

    void process_gpu(const VividGpuContext* ctx) override {
        // Check input scene
        if (ctx->custom_input_count == 0 || !vivid::gpu::scene_input(ctx, 0)) return;

        const auto* input = vivid::gpu::scene_input(ctx, 0);
        if (!input->cpu_vertices || input->cpu_vertex_count == 0) {
            // Pass through unmodified if no CPU vertex data
            ctx->custom_outputs[0] = const_cast<vivid::gpu::VividSceneFragment*>(input);
            return;
        }

        uint32_t vc = input->cpu_vertex_count;
        float time = static_cast<float>(ctx->time);
        float amp  = amplitude.value;
        float freq = frequency.value;
        float spd  = speed.value;
        int   mode_val = mode.int_value();
        int   axis_val = axis.int_value();

        // Audio mode: read from 'amount' input (port index 1, but input_values index 1
        // corresponds to second input port)
        float audio_val = 0.0f;
        if (ctx->input_values) {
            audio_val = ctx->input_values[1];
        }

        // Copy source vertices
        displaced_.resize(vc);
        const vivid::gpu::Vertex3D* src = input->cpu_vertices;

        for (uint32_t i = 0; i < vc; ++i) {
            displaced_[i] = src[i];
            float disp = 0.0f;

            switch (mode_val) {
                case 0: { // Noise
                    float nx = src[i].position[0] * freq + time * spd;
                    float ny = src[i].position[1] * freq + time * spd * 0.7f;
                    float nz = src[i].position[2] * freq + time * spd * 0.3f;
                    disp = simplex3d(nx, ny, nz) * amp;
                    break;
                }
                case 1: { // Sine
                    float coord = 0.0f;
                    switch (axis_val) {
                        case 0: // All — use magnitude
                            coord = std::sqrt(src[i].position[0]*src[i].position[0] +
                                              src[i].position[1]*src[i].position[1] +
                                              src[i].position[2]*src[i].position[2]);
                            break;
                        case 1: coord = src[i].position[0]; break; // X
                        case 2: coord = src[i].position[1]; break; // Y
                        case 3: coord = src[i].position[2]; break; // Z
                    }
                    disp = std::sin(coord * freq + time * spd) * amp;
                    break;
                }
                case 2: { // Audio
                    disp = audio_val * amp;
                    break;
                }
            }

            // Displace along normal
            displaced_[i].position[0] += src[i].normal[0] * disp;
            displaced_[i].position[1] += src[i].normal[1] * disp;
            displaced_[i].position[2] += src[i].normal[2] * disp;
        }

        // Upload to GPU — reuse buffer if size unchanged, else recreate
        uint64_t needed = vc * sizeof(vivid::gpu::Vertex3D);
        if (needed != vb_size_) {
            vivid::gpu::release(vertex_buffer_);
            vertex_buffer_ = vivid::gpu::create_vertex_buffer(
                ctx->device, ctx->queue, displaced_.data(), needed, "Deformer VB");
            vb_size_ = needed;
        } else {
            wgpuQueueWriteBuffer(ctx->queue, vertex_buffer_, 0,
                                 displaced_.data(), needed);
        }

        // Output: shallow copy of input fragment with replaced vertex buffer
        fragment_ = *input;
        fragment_.vertex_buffer    = vertex_buffer_;
        fragment_.vertex_buf_size  = needed;
        fragment_.cpu_vertices     = displaced_.data();
        fragment_.cpu_vertex_count = vc;

        ctx->custom_outputs[0] = &fragment_;

        // Animated 3D thumbnail: the deformed mesh (index buffer passthrough from the input fragment).
        float bmin[3], bmax[3];
        if (vivid::thumb3d::aabb_from_verts(fragment_.cpu_vertices, fragment_.cpu_vertex_count, bmin, bmax))
            vivid::thumb3d::render(ctx, thumb_, fragment_.vertex_buffer, fragment_.vertex_buf_size,
                                   fragment_.index_buffer, fragment_.index_count, bmin, bmax, fragment_.color);
    }

    ~Deformer() override {
        vivid::gpu::release(vertex_buffer_);
        vivid::thumb3d::destroy(thumb_);
    }

private:
    vivid::gpu::VividSceneFragment fragment_{};
    vivid::thumb3d::State thumb_{};
    std::vector<vivid::gpu::Vertex3D> displaced_;
    WGPUBuffer vertex_buffer_ = nullptr;
    uint64_t   vb_size_       = 0;
};

VIVID_REGISTER(Deformer)
VIVID_THUMBNAIL(Deformer)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividSceneFragment)
