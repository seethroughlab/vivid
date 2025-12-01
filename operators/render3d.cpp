// Render3D Operator
// Renders 3D geometry with materials and lighting to a texture

#include <vivid/vivid.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace vivid;

/**
 * @brief 3D scene rendering operator.
 *
 * Combines geometry, material, lighting, and camera into a rendered texture.
 * Supports Phong and PBR shading models with optional IBL.
 *
 * Usage in chain.cpp:
 * @code
 * chain.add<Render3D>("scene")
 *     .primitive(Render3D::Sphere)    // or Cube, Plane, Torus, Cylinder
 *     .shading(Render3D::PBR)         // or Phong
 *     .albedo(1.0f, 0.2f, 0.2f)       // Red
 *     .metallic(0.8f)
 *     .roughness(0.2f)
 *     .lightPreset(Render3D::ThreePoint);
 * @endcode
 */
class Render3D : public Operator {
public:
    // Primitive types
    enum Primitive { Cube = 0, Sphere = 1, Plane = 2, Torus = 3, Cylinder = 4, Cone = 5 };

    // Shading models
    enum Shading { Unlit = 0, Phong = 1, PBR = 2, PBR_IBL = 3 };

    // Light presets
    enum LightPreset { Custom = 0, Outdoor = 1, Indoor = 2, ThreePoint = 3, Studio = 4 };

    Render3D() = default;

    // Fluent API - Geometry
    Render3D& primitive(Primitive p) { primitive_ = p; meshDirty_ = true; return *this; }
    Render3D& primitive(int p) { primitive_ = static_cast<Primitive>(p); meshDirty_ = true; return *this; }

    // Fluent API - Transform
    Render3D& position(float x, float y, float z) { position_ = {x, y, z}; return *this; }
    Render3D& position(const glm::vec3& p) { position_ = p; return *this; }
    Render3D& rotation(float x, float y, float z) { rotation_ = {x, y, z}; return *this; }
    Render3D& rotation(const glm::vec3& r) { rotation_ = r; return *this; }
    Render3D& scale(float s) { scale_ = {s, s, s}; return *this; }
    Render3D& scale(float x, float y, float z) { scale_ = {x, y, z}; return *this; }
    Render3D& scale(const glm::vec3& s) { scale_ = s; return *this; }
    Render3D& autoRotate(float speed) { autoRotateSpeed_ = speed; return *this; }

    // Fluent API - Camera
    Render3D& cameraPosition(float x, float y, float z) { cameraPos_ = {x, y, z}; return *this; }
    Render3D& cameraPosition(const glm::vec3& p) { cameraPos_ = p; return *this; }
    Render3D& cameraTarget(float x, float y, float z) { cameraTarget_ = {x, y, z}; return *this; }
    Render3D& cameraTarget(const glm::vec3& t) { cameraTarget_ = t; return *this; }
    Render3D& fov(float f) { fov_ = f; return *this; }
    Render3D& nearPlane(float n) { nearPlane_ = n; return *this; }
    Render3D& farPlane(float f) { farPlane_ = f; return *this; }
    Render3D& orbitCamera(bool enable) { orbitCamera_ = enable; return *this; }
    Render3D& orbitSpeed(float s) { orbitSpeed_ = s; return *this; }
    Render3D& orbitDistance(float d) { orbitDistance_ = d; return *this; }

    // Fluent API - Shading
    Render3D& shading(Shading s) { shading_ = s; return *this; }
    Render3D& shading(int s) { shading_ = static_cast<Shading>(s); return *this; }

    // Fluent API - Phong Material
    Render3D& ambient(float r, float g, float b) { phongMat_.ambient = {r, g, b}; return *this; }
    Render3D& diffuse(float r, float g, float b) { phongMat_.diffuse = {r, g, b}; return *this; }
    Render3D& specular(float r, float g, float b) { phongMat_.specular = {r, g, b}; return *this; }
    Render3D& shininess(float s) { phongMat_.shininess = s; return *this; }

    // Fluent API - PBR Material
    Render3D& albedo(float r, float g, float b) { pbrMat_.albedo = {r, g, b}; return *this; }
    Render3D& albedo(const glm::vec3& c) { pbrMat_.albedo = c; return *this; }
    Render3D& metallic(float m) { pbrMat_.metallic = m; return *this; }
    Render3D& roughness(float r) { pbrMat_.roughness = r; return *this; }
    Render3D& ao(float a) { pbrMat_.ao = a; return *this; }
    Render3D& emissive(float r, float g, float b) {
        phongMat_.emissive = {r, g, b};
        pbrMat_.emissive = {r, g, b};
        return *this;
    }

    // Fluent API - Lighting
    Render3D& lightPreset(LightPreset p) { lightPreset_ = p; return *this; }
    Render3D& lightPreset(int p) { lightPreset_ = static_cast<LightPreset>(p); return *this; }
    Render3D& ambientColor(float r, float g, float b) { lighting_.ambientColor = {r, g, b}; return *this; }
    Render3D& ambientIntensity(float i) { lighting_.ambientIntensity = i; return *this; }

    // Fluent API - Add individual lights
    Render3D& addDirectionalLight(const glm::vec3& dir, const glm::vec3& color = {1,1,1}, float intensity = 1.0f) {
        lighting_.addLight(Light::directional(dir, color, intensity));
        lightPreset_ = Custom;
        return *this;
    }
    Render3D& addPointLight(const glm::vec3& pos, const glm::vec3& color = {1,1,1}, float intensity = 1.0f, float radius = 10.0f) {
        lighting_.addLight(Light::point(pos, color, intensity, radius));
        lightPreset_ = Custom;
        return *this;
    }
    Render3D& addSpotLight(const glm::vec3& pos, const glm::vec3& dir, float innerAngle = 15.0f, float outerAngle = 30.0f,
                           const glm::vec3& color = {1,1,1}, float intensity = 1.0f) {
        lighting_.addLight(Light::spot(pos, dir, innerAngle, outerAngle, color, intensity));
        lightPreset_ = Custom;
        return *this;
    }
    Render3D& clearLights() { lighting_.clearLights(); return *this; }

    // Fluent API - IBL/Environment
    Render3D& environment(const std::string& hdrPath) { envPath_ = hdrPath; return *this; }
    Render3D& envIntensity(float i) { envIntensity_ = i; return *this; }

    // Fluent API - Background
    Render3D& clearColor(float r, float g, float b, float a = 1.0f) { clearColor_ = {r, g, b, a}; return *this; }
    Render3D& clearColor(const glm::vec4& c) { clearColor_ = c; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
        createMesh(ctx);

        // Load environment if specified
        if (!envPath_.empty()) {
            env_ = ctx.loadEnvironment(ctx.resolvePath(envPath_));
            if (!env_.valid()) {
                std::cerr << "[Render3D] Failed to load environment: " << envPath_ << "\n";
            }
        }
    }

    void process(Context& ctx) override {
        // Recreate mesh if primitive changed
        if (meshDirty_) {
            if (mesh_.valid()) {
                ctx.destroyMesh(mesh_);
            }
            createMesh(ctx);
            meshDirty_ = false;
        }

        if (!mesh_.valid()) return;

        // Update auto-rotation
        if (autoRotateSpeed_ != 0.0f) {
            autoRotateAngle_ += ctx.dt() * autoRotateSpeed_;
        }

        // Update orbit camera
        if (orbitCamera_) {
            orbitAngle_ += ctx.dt() * orbitSpeed_;
            cameraPos_.x = cameraTarget_.x + orbitDistance_ * std::sin(orbitAngle_);
            cameraPos_.z = cameraTarget_.z + orbitDistance_ * std::cos(orbitAngle_);
        }

        // Build model transform
        glm::mat4 model(1.0f);
        model = glm::translate(model, position_);
        model = glm::rotate(model, rotation_.y + autoRotateAngle_, glm::vec3(0, 1, 0));
        model = glm::rotate(model, rotation_.x, glm::vec3(1, 0, 0));
        model = glm::rotate(model, rotation_.z, glm::vec3(0, 0, 1));
        model = glm::scale(model, scale_);

        // Setup camera
        Camera3D camera;
        camera.position = cameraPos_;
        camera.target = cameraTarget_;
        camera.fov = fov_;
        camera.nearPlane = nearPlane_;
        camera.farPlane = farPlane_;

        // Apply light preset if not custom
        applyLightPreset();

        // Render based on shading mode
        switch (shading_) {
            case Unlit:
                ctx.render3D(mesh_, camera, model, output_, clearColor_);
                break;

            case Phong:
                ctx.render3DPhong(mesh_, camera, model, phongMat_, lighting_, output_, clearColor_);
                break;

            case PBR:
                ctx.render3DPBR(mesh_, camera, model, pbrMat_, lighting_, output_, clearColor_);
                break;

            case PBR_IBL:
                if (env_.valid()) {
                    Environment envCopy = env_;
                    envCopy.intensity = envIntensity_;
                    ctx.render3DPBR(mesh_, camera, model, pbrMat_, lighting_, envCopy, output_, clearColor_);
                } else {
                    // Fallback to PBR without IBL
                    ctx.render3DPBR(mesh_, camera, model, pbrMat_, lighting_, output_, clearColor_);
                }
                break;
        }

        ctx.setOutput("out", output_);
    }

    void cleanup() override {
        // Mesh and environment cleanup handled by renderer when Context is destroyed
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("primitive", static_cast<int>(primitive_), 0, 5),
            intParam("shading", static_cast<int>(shading_), 0, 3),
            floatParam("metallic", pbrMat_.metallic, 0.0f, 1.0f),
            floatParam("roughness", pbrMat_.roughness, 0.0f, 1.0f),
            intParam("lightPreset", static_cast<int>(lightPreset_), 0, 4),
            floatParam("autoRotate", autoRotateSpeed_, -5.0f, 5.0f),
            floatParam("fov", fov_, 10.0f, 120.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    void createMesh(Context& ctx) {
        switch (primitive_) {
            case Cube:
                mesh_ = ctx.createCube();
                break;
            case Sphere:
                mesh_ = ctx.createSphere(0.5f, 32, 16);
                break;
            case Plane:
                mesh_ = ctx.createPlane(1.0f, 1.0f);
                break;
            case Torus:
                mesh_ = ctx.createTorus(0.4f, 0.15f);
                break;
            case Cylinder:
                mesh_ = ctx.createCylinder(0.4f, 1.0f, 32);
                break;
            case Cone:
                // Use cylinder with zero top radius (need to implement cone or use cylinder)
                mesh_ = ctx.createCylinder(0.4f, 1.0f, 32);
                break;
        }
    }

    void applyLightPreset() {
        if (lightPreset_ == Custom) return;

        // Only apply preset if lights are empty or preset changed
        lighting_.clearLights();

        switch (lightPreset_) {
            case Outdoor:
                lighting_ = SceneLighting::outdoor();
                break;
            case Indoor:
                lighting_ = SceneLighting::indoor();
                break;
            case ThreePoint:
                lighting_ = SceneLighting::threePoint();
                break;
            case Studio: {
                lighting_.ambientColor = glm::vec3(0.2f);
                lighting_.ambientIntensity = 0.4f;
                // Key light from upper front-left
                lighting_.addLight(Light::directional(glm::vec3(-0.5f, -0.8f, -0.5f), glm::vec3(1.0f), 1.2f));
                // Fill light from right
                lighting_.addLight(Light::directional(glm::vec3(0.6f, -0.4f, 0.2f), glm::vec3(0.8f, 0.85f, 0.9f), 0.5f));
                // Rim/back light
                lighting_.addLight(Light::directional(glm::vec3(0.0f, -0.3f, 1.0f), glm::vec3(1.0f), 0.7f));
                break;
            }
            case Custom:
                break;
        }
    }

    // Geometry
    Primitive primitive_ = Sphere;
    Mesh3D mesh_;
    bool meshDirty_ = false;

    // Transform
    glm::vec3 position_{0.0f};
    glm::vec3 rotation_{0.0f};
    glm::vec3 scale_{1.0f};
    float autoRotateSpeed_ = 0.0f;
    float autoRotateAngle_ = 0.0f;

    // Camera
    glm::vec3 cameraPos_{0.0f, 0.0f, 3.0f};
    glm::vec3 cameraTarget_{0.0f};
    float fov_ = 60.0f;
    float nearPlane_ = 0.1f;
    float farPlane_ = 100.0f;
    bool orbitCamera_ = false;
    float orbitSpeed_ = 0.5f;
    float orbitDistance_ = 3.0f;
    float orbitAngle_ = 0.0f;

    // Shading
    Shading shading_ = PBR;

    // Materials
    PhongMaterial phongMat_ = PhongMaterial::shiny(glm::vec3(0.8f));
    PBRMaterial pbrMat_ = PBRMaterial::plastic(glm::vec3(0.8f));

    // Lighting
    LightPreset lightPreset_ = ThreePoint;
    SceneLighting lighting_;

    // Environment/IBL
    std::string envPath_;
    Environment env_;
    float envIntensity_ = 1.0f;

    // Output
    glm::vec4 clearColor_{0.1f, 0.1f, 0.15f, 1.0f};
    Texture output_;
};

VIVID_OPERATOR(Render3D)
