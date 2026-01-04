// CurlNoiseForce implementation
// Uses 3D Perlin noise with curl operation for divergence-free flow

#include <vivid/effects/forces/curl_noise_force.h>
#include <vivid/effects/particle_system.h>  // For Particle struct
#include <cmath>

namespace vivid::effects {

// Perlin noise helpers (same as in particle_system.cpp)
namespace {

constexpr int perm[512] = {
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

inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float lerp(float a, float b, float t) { return a + t * (b - a); }

inline float grad3(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float snoise3(float x, float y, float z) {
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;
    int Z = static_cast<int>(std::floor(z)) & 255;

    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);

    float u = fade(x);
    float v = fade(y);
    float w = fade(z);

    int A = perm[X] + Y;
    int AA = perm[A] + Z;
    int AB = perm[A + 1] + Z;
    int B = perm[X + 1] + Y;
    int BA = perm[B] + Z;
    int BB = perm[B + 1] + Z;

    return lerp(lerp(lerp(grad3(perm[AA], x, y, z),
                          grad3(perm[BA], x - 1, y, z), u),
                     lerp(grad3(perm[AB], x, y - 1, z),
                          grad3(perm[BB], x - 1, y - 1, z), u), v),
                lerp(lerp(grad3(perm[AA + 1], x, y, z - 1),
                          grad3(perm[BA + 1], x - 1, y, z - 1), u),
                     lerp(grad3(perm[AB + 1], x, y - 1, z - 1),
                          grad3(perm[BB + 1], x - 1, y - 1, z - 1), u), v), w);
}

float fbm3(float x, float y, float z, int octaves) {
    float result = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmp = 0.0f;

    for (int i = 0; i < octaves; i++) {
        result += amplitude * snoise3(x * frequency, y * frequency, z * frequency);
        maxAmp += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return result / maxAmp;
}

} // anonymous namespace

glm::vec3 CurlNoiseForce::compute(const Particle& p, float time, float dt) {
    float s = static_cast<float>(strength);
    if (s < 0.0001f) return glm::vec3(0.0f);

    float scaleVal = static_cast<float>(scale);
    float speedVal = static_cast<float>(speed);
    int octavesVal = static_cast<int>(octaves);

    const float e = 0.001f;
    glm::vec3 pos = p.position * scaleVal;
    float t = (time + p.seed * 10.0f) * speedVal;

    // Potential function using FBM noise
    auto potential = [&](glm::vec3 q) -> glm::vec3 {
        return glm::vec3(
            fbm3(q.x + t, q.y + 100.0f, q.z, octavesVal),
            fbm3(q.x + 200.0f, q.y + t, q.z + 100.0f, octavesVal),
            is3D ? fbm3(q.x + 100.0f, q.y + 300.0f, q.z + t, octavesVal) : 0.0f
        );
    };

    // Finite differences to compute curl
    glm::vec3 dx = potential(pos + glm::vec3(e, 0, 0)) - potential(pos - glm::vec3(e, 0, 0));
    glm::vec3 dy = potential(pos + glm::vec3(0, e, 0)) - potential(pos - glm::vec3(0, e, 0));

    glm::vec3 curl;
    if (is3D) {
        glm::vec3 dz = potential(pos + glm::vec3(0, 0, e)) - potential(pos - glm::vec3(0, 0, e));
        float d = 2.0f * e;
        curl = glm::vec3(
            dy.z / d - dz.y / d,
            dz.x / d - dx.z / d,
            dx.y / d - dy.x / d
        );
    } else {
        // 2D curl (perpendicular to gradient)
        float d = 2.0f * e;
        curl = glm::vec3(dy.x / d, -dx.x / d, 0.0f);
    }

    return curl * s * dt;
}

} // namespace vivid::effects
