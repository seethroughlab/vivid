#pragma once

// Vivid Effects 2D - Noise Operator
// Generates animated fractal noise with multiple algorithms

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

// Noise algorithm types
enum class NoiseType {
    Perlin,     // Classic gradient noise - smooth, natural looking
    Simplex,    // Improved gradient noise - fewer artifacts, faster
    Worley,     // Cellular/Voronoi noise - organic cell patterns
    Value       // Simple interpolated random values - blocky, retro
};

class Noise : public TextureOperator {
public:
    Noise() = default;
    ~Noise() override;

    // Fluent API
    Noise& type(NoiseType t) { m_type = t; return *this; }
    Noise& scale(float s) { m_scale = s; return *this; }
    Noise& speed(float s) { m_speed = s; return *this; }
    Noise& octaves(int o) { m_octaves = o; return *this; }
    Noise& lacunarity(float l) { m_lacunarity = l; return *this; }
    Noise& persistence(float p) { m_persistence = p; return *this; }
    Noise& offset(float x, float y) { m_offset.set(x, y); return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Noise"; }

    // Parameter declarations for UI
    std::vector<ParamDecl> params() override {
        return {
            m_scale.decl(), m_speed.decl(), m_octaves.decl(),
            m_lacunarity.decl(), m_persistence.decl(), m_offset.decl()
        };
    }

private:
    void createPipeline(Context& ctx);

    // Parameters
    NoiseType m_type = NoiseType::Perlin;
    Param<float> m_scale{"scale", 4.0f, 0.1f, 20.0f};
    Param<float> m_speed{"speed", 0.5f, 0.0f, 5.0f};
    Param<int> m_octaves{"octaves", 4, 1, 8};
    Param<float> m_lacunarity{"lacunarity", 2.0f, 1.0f, 4.0f};
    Param<float> m_persistence{"persistence", 0.5f, 0.0f, 1.0f};
    Vec2Param m_offset{"offset", 0.0f, 0.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
