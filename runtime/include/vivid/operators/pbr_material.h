#pragma once

#include "../operator.h"
#include "../context.h"
#include "../graphics3d.h"

namespace vivid {

/**
 * @brief PBR material definition operator.
 *
 * Defines a physically-based material using the metallic-roughness workflow.
 * Materials can be reused across multiple render calls.
 *
 * Example:
 * @code
 * void setup(Chain& chain) {
 *     // Define a gold material
 *     chain.add<PBRMat>("gold").gold();
 *
 *     // Or use custom values
 *     chain.add<PBRMat>("custom")
 *         .albedo(1.0f, 0.765f, 0.336f)
 *         .metallic(1.0f)
 *         .roughness(0.2f);
 * }
 *
 * void update(Chain& chain, Context& ctx) {
 *     // Get the material for use with rendering
 *     auto mat = chain.get<PBRMat>("gold").getMaterial();
 *     ctx.render3DPBR(mesh, camera, transform, mat, lighting, output);
 * }
 * @endcode
 */
class PBRMat : public Operator {
public:
    PBRMat() = default;

    // Fluent API - Base Properties
    PBRMat& albedo(float r, float g, float b) {
        mat_.albedo = glm::vec3(r, g, b);
        return *this;
    }
    PBRMat& albedo(const glm::vec3& c) {
        mat_.albedo = c;
        return *this;
    }
    PBRMat& metallic(float m) {
        mat_.metallic = m;
        return *this;
    }
    PBRMat& roughness(float r) {
        mat_.roughness = r;
        return *this;
    }
    PBRMat& ao(float a) {
        mat_.ao = a;
        return *this;
    }
    PBRMat& emissive(float r, float g, float b) {
        mat_.emissive = glm::vec3(r, g, b);
        return *this;
    }
    PBRMat& emissive(const glm::vec3& c) {
        mat_.emissive = c;
        return *this;
    }

    // Presets (match PBRMaterial static methods)
    PBRMat& plastic(const glm::vec3& color) {
        mat_ = PBRMaterial::plastic(color);
        return *this;
    }
    PBRMat& plastic(float r, float g, float b) {
        return plastic(glm::vec3(r, g, b));
    }
    PBRMat& metal(const glm::vec3& color, float rough = 0.3f) {
        mat_ = PBRMaterial::metal(color, rough);
        return *this;
    }
    PBRMat& metal(float r, float g, float b, float rough = 0.3f) {
        return metal(glm::vec3(r, g, b), rough);
    }
    PBRMat& rubber(const glm::vec3& color) {
        mat_ = PBRMaterial::rubber(color);
        return *this;
    }
    PBRMat& rubber(float r, float g, float b) {
        return rubber(glm::vec3(r, g, b));
    }

    // Named metal presets
    PBRMat& gold() {
        mat_ = PBRMaterial::gold();
        return *this;
    }
    PBRMat& silver() {
        mat_ = PBRMaterial::silver();
        return *this;
    }
    PBRMat& copper() {
        mat_ = PBRMaterial::copper();
        return *this;
    }
    PBRMat& iron(float rust = 0.0f) {
        mat_.albedo = glm::mix(glm::vec3(0.560f, 0.570f, 0.580f), glm::vec3(0.518f, 0.314f, 0.227f), rust);
        mat_.metallic = 1.0f - rust * 0.3f;
        mat_.roughness = 0.3f + rust * 0.4f;
        return *this;
    }
    PBRMat& aluminum() {
        mat_.albedo = glm::vec3(0.913f, 0.921f, 0.925f);
        mat_.metallic = 1.0f;
        mat_.roughness = 0.15f;
        return *this;
    }
    PBRMat& chrome() {
        mat_.albedo = glm::vec3(0.549f, 0.556f, 0.554f);
        mat_.metallic = 1.0f;
        mat_.roughness = 0.05f;
        return *this;
    }

    // Dielectric presets
    PBRMat& glass() {
        mat_.albedo = glm::vec3(0.95f, 0.95f, 0.95f);
        mat_.metallic = 0.0f;
        mat_.roughness = 0.0f;
        return *this;
    }
    PBRMat& ceramic(const glm::vec3& color = glm::vec3(0.95f)) {
        mat_.albedo = color;
        mat_.metallic = 0.0f;
        mat_.roughness = 0.3f;
        return *this;
    }
    PBRMat& wood(const glm::vec3& color = glm::vec3(0.4f, 0.25f, 0.12f)) {
        mat_.albedo = color;
        mat_.metallic = 0.0f;
        mat_.roughness = 0.65f;
        return *this;
    }
    PBRMat& fabric(const glm::vec3& color) {
        mat_.albedo = color;
        mat_.metallic = 0.0f;
        mat_.roughness = 0.9f;
        return *this;
    }
    PBRMat& skin(const glm::vec3& color = glm::vec3(0.8f, 0.6f, 0.5f)) {
        mat_.albedo = color;
        mat_.metallic = 0.0f;
        mat_.roughness = 0.55f;
        return *this;
    }

    // Get the material struct for use with render functions
    const PBRMaterial& getMaterial() const { return mat_; }
    PBRMaterial& getMaterial() { return mat_; }

    void init(Context& ctx) override {
        // No initialization needed
    }

    void process(Context& ctx) override {
        // Material operators don't produce texture output - they just hold data
        // The material is accessed via getMaterial() in user code
    }

    std::vector<ParamDecl> params() override {
        return {
            colorParam("albedo", mat_.albedo),
            floatParam("metallic", mat_.metallic, 0.0f, 1.0f),
            floatParam("roughness", mat_.roughness, 0.0f, 1.0f),
            floatParam("ao", mat_.ao, 0.0f, 1.0f),
            colorParam("emissive", mat_.emissive)
        };
    }

    OutputKind outputKind() override { return OutputKind::Value; }

private:
    PBRMaterial mat_;
};

} // namespace vivid
