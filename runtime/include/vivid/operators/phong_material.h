#pragma once

#include "../operator.h"
#include "../context.h"
#include "../graphics3d.h"

namespace vivid {

/**
 * @brief Phong material definition operator.
 *
 * Defines a classic ambient/diffuse/specular material for 3D rendering.
 * Materials can be reused across multiple render calls.
 *
 * Example:
 * @code
 * void setup(Chain& chain) {
 *     // Define a shiny red material
 *     chain.add<PhongMat>("redShiny")
 *         .diffuse(0.8f, 0.1f, 0.1f)
 *         .specular(1.0f, 1.0f, 1.0f)
 *         .shininess(64.0f);
 *
 *     // Or use a preset
 *     chain.add<PhongMat>("matte").matte(0.5f, 0.5f, 0.8f);
 * }
 *
 * void update(Chain& chain, Context& ctx) {
 *     // Get the material for use with rendering
 *     auto mat = chain.get<PhongMat>("redShiny").getMaterial();
 *     ctx.render3DPhong(mesh, camera, transform, mat, lighting, output);
 * }
 * @endcode
 */
class PhongMat : public Operator {
public:
    PhongMat() = default;

    // Fluent API - Colors
    PhongMat& ambient(float r, float g, float b) {
        mat_.ambient = glm::vec3(r, g, b);
        return *this;
    }
    PhongMat& ambient(const glm::vec3& c) {
        mat_.ambient = c;
        return *this;
    }
    PhongMat& diffuse(float r, float g, float b) {
        mat_.diffuse = glm::vec3(r, g, b);
        return *this;
    }
    PhongMat& diffuse(const glm::vec3& c) {
        mat_.diffuse = c;
        return *this;
    }
    PhongMat& specular(float r, float g, float b) {
        mat_.specular = glm::vec3(r, g, b);
        return *this;
    }
    PhongMat& specular(const glm::vec3& c) {
        mat_.specular = c;
        return *this;
    }
    PhongMat& emissive(float r, float g, float b) {
        mat_.emissive = glm::vec3(r, g, b);
        return *this;
    }
    PhongMat& emissive(const glm::vec3& c) {
        mat_.emissive = c;
        return *this;
    }

    // Fluent API - Parameters
    PhongMat& shininess(float s) {
        mat_.shininess = s;
        return *this;
    }

    // Presets (match PhongMaterial static methods)
    PhongMat& matte(const glm::vec3& color) {
        mat_ = PhongMaterial::matte(color);
        return *this;
    }
    PhongMat& matte(float r, float g, float b) {
        return matte(glm::vec3(r, g, b));
    }
    PhongMat& shiny(const glm::vec3& color) {
        mat_ = PhongMaterial::shiny(color);
        return *this;
    }
    PhongMat& shiny(float r, float g, float b) {
        return shiny(glm::vec3(r, g, b));
    }
    PhongMat& metallic(const glm::vec3& color) {
        mat_ = PhongMaterial::metallic(color);
        return *this;
    }
    PhongMat& metallic(float r, float g, float b) {
        return metallic(glm::vec3(r, g, b));
    }

    // Get the material struct for use with render functions
    const PhongMaterial& getMaterial() const { return mat_; }
    PhongMaterial& getMaterial() { return mat_; }

    void init(Context& ctx) override {
        // No initialization needed
    }

    void process(Context& ctx) override {
        // Material operators don't produce texture output - they just hold data
        // The material is accessed via getMaterial() in user code
    }

    std::vector<ParamDecl> params() override {
        return {
            colorParam("ambient", mat_.ambient),
            colorParam("diffuse", mat_.diffuse),
            colorParam("specular", mat_.specular),
            colorParam("emissive", mat_.emissive),
            floatParam("shininess", mat_.shininess, 1.0f, 256.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Value; }

private:
    PhongMaterial mat_;
};

} // namespace vivid
