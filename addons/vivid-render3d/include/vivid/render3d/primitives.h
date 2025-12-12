#pragma once

/**
 * @file primitives.h
 * @brief Primitive mesh operators
 *
 * Provides MeshOperator implementations for common 3D primitives:
 * - Box
 * - Sphere
 * - Cylinder
 * - Cone
 * - Torus
 * - Plane
 *
 * Each primitive uses MeshBuilder internally and outputs a Mesh that can
 * be fed into Boolean operators or SceneComposer.
 */

#include <vivid/render3d/mesh_operator.h>
#include <vivid/render3d/mesh_builder.h>
#include <vivid/context.h>

namespace vivid::render3d {

// =============================================================================
// Box
// =============================================================================

/**
 * @brief Box/cube mesh generator
 *
 * Creates a box with specified dimensions.
 *
 * @par Example
 * @code
 * auto& box = chain.add<Box>("box")
 *     .size(1.0f, 2.0f, 1.0f)
 *     .flatShading(true);
 * @endcode
 */
class Box : public MeshOperator {
public:
    /// Set box dimensions
    Box& size(float width, float height, float depth) {
        if (m_width != width || m_height != height || m_depth != depth) {
            m_width = width; m_height = height; m_depth = depth; markDirty();
        }
        return *this;
    }

    /// Set uniform size (cube)
    Box& size(float s) {
        return size(s, s, s);
    }

    /// Enable flat shading (faceted look)
    Box& flatShading(bool enabled) {
        if (m_flatShading != enabled) { m_flatShading = enabled; markDirty(); }
        return *this;
    }

    /// Enable tangent computation (required for normal mapping)
    Box& computeTangents() {
        if (!m_computeTangents) { m_computeTangents = true; markDirty(); }
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        if (!needsCook()) return;

        m_builder = MeshBuilder::box(m_width, m_height, m_depth);
        // Box always uses flat normals - smooth shading doesn't make sense for a cube
        m_builder.computeFlatNormals();
        if (m_computeTangents) {
            m_builder.computeTangents();
        }
        m_mesh = m_builder.build();
        m_mesh.upload(ctx);

        didCook();
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "Box"; }

private:
    float m_width = 1.0f;
    float m_height = 1.0f;
    float m_depth = 1.0f;
    bool m_flatShading = true;
    bool m_computeTangents = false;
};

// =============================================================================
// Sphere
// =============================================================================

/**
 * @brief Sphere mesh generator
 *
 * Creates a UV sphere with specified radius and detail level.
 *
 * @par Example
 * @code
 * auto& sphere = chain.add<Sphere>("sphere")
 *     .radius(0.5f)
 *     .segments(32);
 * @endcode
 */
class Sphere : public MeshOperator {
public:
    /// Set sphere radius
    Sphere& radius(float r) {
        if (m_radius != r) { m_radius = r; markDirty(); }
        return *this;
    }

    /// Set number of segments (detail level)
    Sphere& segments(int s) {
        if (m_segments != s) { m_segments = s; markDirty(); }
        return *this;
    }

    /// Enable tangent computation (required for normal mapping)
    Sphere& computeTangents() {
        if (!m_computeTangents) { m_computeTangents = true; markDirty(); }
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        if (!needsCook()) return;

        m_builder = MeshBuilder::sphere(m_radius, m_segments);
        if (m_computeTangents) {
            m_builder.computeTangents();
        }
        m_mesh = m_builder.build();
        m_mesh.upload(ctx);

        didCook();
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "Sphere"; }

private:
    float m_radius = 0.5f;
    int m_segments = 24;
    bool m_computeTangents = false;
};

// =============================================================================
// Cylinder
// =============================================================================

/**
 * @brief Cylinder mesh generator
 *
 * Creates a cylinder with specified radius, height, and detail level.
 *
 * @par Example
 * @code
 * auto& cylinder = chain.add<Cylinder>("cylinder")
 *     .radius(0.5f)
 *     .height(2.0f)
 *     .segments(24);
 * @endcode
 */
class Cylinder : public MeshOperator {
public:
    /// Set cylinder radius
    Cylinder& radius(float r) {
        if (m_radius != r) { m_radius = r; markDirty(); }
        return *this;
    }

    /// Set cylinder height
    Cylinder& height(float h) {
        if (m_height != h) { m_height = h; markDirty(); }
        return *this;
    }

    /// Set number of segments (detail level)
    Cylinder& segments(int s) {
        if (m_segments != s) { m_segments = s; markDirty(); }
        return *this;
    }

    /// Enable flat shading
    Cylinder& flatShading(bool enabled) {
        if (m_flatShading != enabled) { m_flatShading = enabled; markDirty(); }
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        if (!needsCook()) return;

        m_builder = MeshBuilder::cylinder(m_radius, m_height, m_segments);
        if (m_flatShading) {
            m_builder.computeFlatNormals();
        } else {
            m_builder.computeNormals();
        }
        m_mesh = m_builder.build();
        m_mesh.upload(ctx);

        didCook();
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "Cylinder"; }

private:
    float m_radius = 0.5f;
    float m_height = 1.0f;
    int m_segments = 24;
    bool m_flatShading = false;
};

// =============================================================================
// Cone
// =============================================================================

/**
 * @brief Cone mesh generator
 *
 * Creates a cone with specified base radius, height, and detail level.
 *
 * @par Example
 * @code
 * auto& cone = chain.add<Cone>("cone")
 *     .radius(0.5f)
 *     .height(1.0f)
 *     .segments(24);
 * @endcode
 */
class Cone : public MeshOperator {
public:
    /// Set cone base radius
    Cone& radius(float r) {
        if (m_radius != r) { m_radius = r; markDirty(); }
        return *this;
    }

    /// Set cone height
    Cone& height(float h) {
        if (m_height != h) { m_height = h; markDirty(); }
        return *this;
    }

    /// Set number of segments (detail level)
    Cone& segments(int s) {
        if (m_segments != s) { m_segments = s; markDirty(); }
        return *this;
    }

    /// Enable flat shading
    Cone& flatShading(bool enabled) {
        if (m_flatShading != enabled) { m_flatShading = enabled; markDirty(); }
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        if (!needsCook()) return;

        m_builder = MeshBuilder::cone(m_radius, m_height, m_segments);
        if (m_flatShading) {
            m_builder.computeFlatNormals();
        } else {
            m_builder.computeNormals();
        }
        m_mesh = m_builder.build();
        m_mesh.upload(ctx);

        didCook();
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "Cone"; }

private:
    float m_radius = 0.5f;
    float m_height = 1.0f;
    int m_segments = 24;
    bool m_flatShading = true;
};

// =============================================================================
// Torus
// =============================================================================

/**
 * @brief Torus (donut) mesh generator
 *
 * Creates a torus with specified outer/inner radii and detail levels.
 *
 * @par Example
 * @code
 * auto& torus = chain.add<Torus>("torus")
 *     .outerRadius(0.5f)
 *     .innerRadius(0.2f)
 *     .segments(32)
 *     .rings(16);
 * @endcode
 */
class Torus : public MeshOperator {
public:
    /// Set outer radius (distance from center to tube center)
    Torus& outerRadius(float r) {
        if (m_outerRadius != r) { m_outerRadius = r; markDirty(); }
        return *this;
    }

    /// Set inner radius (tube radius)
    Torus& innerRadius(float r) {
        if (m_innerRadius != r) { m_innerRadius = r; markDirty(); }
        return *this;
    }

    /// Set number of segments around the ring
    Torus& segments(int s) {
        if (m_segments != s) { m_segments = s; markDirty(); }
        return *this;
    }

    /// Set number of rings around the tube
    Torus& rings(int r) {
        if (m_rings != r) { m_rings = r; markDirty(); }
        return *this;
    }

    /// Enable tangent computation (required for normal mapping)
    Torus& computeTangents() {
        if (!m_computeTangents) { m_computeTangents = true; markDirty(); }
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        if (!needsCook()) return;

        m_builder = MeshBuilder::torus(m_outerRadius, m_innerRadius, m_segments, m_rings);
        if (m_computeTangents) {
            m_builder.computeTangents();
        }
        m_mesh = m_builder.build();
        m_mesh.upload(ctx);

        didCook();
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "Torus"; }

private:
    float m_outerRadius = 0.5f;
    float m_innerRadius = 0.2f;
    int m_segments = 32;
    int m_rings = 16;
    bool m_computeTangents = false;
};

// =============================================================================
// Plane
// =============================================================================

/**
 * @brief Plane mesh generator
 *
 * Creates a flat plane with optional subdivisions for displacement effects.
 *
 * @par Example
 * @code
 * auto& plane = chain.add<Plane>("plane")
 *     .size(10.0f, 10.0f)
 *     .subdivisions(16, 16);
 * @endcode
 */
class Plane : public MeshOperator {
public:
    /// Set plane dimensions
    Plane& size(float width, float height) {
        if (m_width != width || m_height != height) {
            m_width = width; m_height = height; markDirty();
        }
        return *this;
    }

    /// Set number of subdivisions
    Plane& subdivisions(int x, int y) {
        if (m_subdivisionsX != x || m_subdivisionsY != y) {
            m_subdivisionsX = x; m_subdivisionsY = y; markDirty();
        }
        return *this;
    }

    /// Enable flat shading
    Plane& flatShading(bool enabled) {
        if (m_flatShading != enabled) { m_flatShading = enabled; markDirty(); }
        return *this;
    }

    /// Enable tangent computation (required for normal mapping)
    Plane& computeTangents() {
        if (!m_computeTangents) { m_computeTangents = true; markDirty(); }
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        if (!needsCook()) return;

        m_builder = MeshBuilder::plane(m_width, m_height, m_subdivisionsX, m_subdivisionsY);
        // Plane always uses flat normals - it's a flat surface
        m_builder.computeFlatNormals();
        if (m_computeTangents) {
            m_builder.computeTangents();
        }
        m_mesh = m_builder.build();
        m_mesh.upload(ctx);

        didCook();
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "Plane"; }

private:
    float m_width = 1.0f;
    float m_height = 1.0f;
    int m_subdivisionsX = 1;
    int m_subdivisionsY = 1;
    bool m_flatShading = false;
    bool m_computeTangents = false;
};

} // namespace vivid::render3d
