// Render3D Demo - 3D rendering with PBR materials and lighting
#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

void setup(Chain& chain) {
    // Create a sphere with PBR materials
    chain.add<Render3D>("sphere")
        .primitive(Render3D::Sphere)
        .shading(Render3D::PBR)
        .lightPreset(Render3D::ThreePoint)
        .albedo(0.9f, 0.3f, 0.2f)
        .metallic(0.0f)
        .roughness(0.4f)
        .cameraDistance(3.0f)
        .autoRotate(true)
        .rotateSpeed(0.5f);

    // Create a metallic torus
    chain.add<Render3D>("torus")
        .primitive(Render3D::Torus)
        .shading(Render3D::PBR)
        .lightPreset(Render3D::Studio)
        .albedo(0.8f, 0.7f, 0.2f)
        .metallic(1.0f)
        .roughness(0.2f)
        .cameraDistance(4.0f)
        .cameraElevation(0.3f)
        .autoRotate(true)
        .rotateSpeed(0.3f);

    // Composite them side by side
    chain.add<Composite>("combined")
        .input("sphere")
        .blend("torus", Composite::Add, 1.0f);

    chain.setOutput("combined");
}

void update(Chain& chain, Context& ctx) {
    // Animate roughness on the sphere
    float roughness = 0.2f + 0.4f * (0.5f + 0.5f * std::sin(ctx.time() * 0.5f));
    chain.get<Render3D>("sphere").roughness(roughness);

    // Cycle through different primitives every 5 seconds
    int primitiveIndex = static_cast<int>(ctx.time() / 5.0f) % 6;
    chain.get<Render3D>("torus").primitive(static_cast<Render3D::Primitive>(primitiveIndex));
}

VIVID_CHAIN(setup, update)
