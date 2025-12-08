#pragma once

/**
 * @file primitives.h
 * @brief Primitive geometry operators
 *
 * Provides GeometryOperator implementations for common 3D primitives:
 * - BoxGeometry
 * - SphereGeometry
 * - CylinderGeometry
 * - ConeGeometry
 * - TorusGeometry
 * - PlaneGeometry
 *
 * Each primitive uses MeshBuilder internally and outputs a Mesh that can
 * be fed into Boolean operators or SceneComposer.
 */

#include <vivid/render3d/geometry_operator.h>
#include <vivid/render3d/mesh_builder.h>
#include <vivid/context.h>

namespace vivid::render3d {

// =============================================================================
// BoxGeometry
// =============================================================================

/**
 * @brief Box/cube geometry generator
 *
 * Creates a box with specified dimensions.
 *
 * @par Example
 * @code
 * auto& box = chain.add<BoxGeometry>("box")
 *     .size(1.0f, 2.0f, 1.0f)
 *     .flatShading(true);
 * @endcode
 */
class BoxGeometry : public GeometryOperator {
public:
    /// Set box dimensions
    BoxGeometry& size(float width, float height, float depth) {
        m_width = width;
        m_height = height;
        m_depth = depth;
        return *this;
    }

    /// Set uniform size (cube)
    BoxGeometry& size(float s) {
        m_width = m_height = m_depth = s;
        return *this;
    }

    /// Enable flat shading (faceted look)
    BoxGeometry& flatShading(bool enabled) {
        m_flatShading = enabled;
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        m_builder = MeshBuilder::box(m_width, m_height, m_depth);
        if (m_flatShading) {
            m_builder.computeFlatNormals();
        } else {
            m_builder.computeNormals();
        }
        m_mesh = m_builder.build();
        m_mesh.upload(ctx);
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "BoxGeometry"; }

private:
    float m_width = 1.0f;
    float m_height = 1.0f;
    float m_depth = 1.0f;
    bool m_flatShading = true;
};

// =============================================================================
// SphereGeometry
// =============================================================================

/**
 * @brief Sphere geometry generator
 *
 * Creates a UV sphere with specified radius and detail level.
 *
 * @par Example
 * @code
 * auto& sphere = chain.add<SphereGeometry>("sphere")
 *     .radius(0.5f)
 *     .segments(32);
 * @endcode
 */
class SphereGeometry : public GeometryOperator {
public:
    /// Set sphere radius
    SphereGeometry& radius(float r) {
        m_radius = r;
        return *this;
    }

    /// Set number of segments (detail level)
    SphereGeometry& segments(int s) {
        m_segments = s;
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        m_builder = MeshBuilder::sphere(m_radius, m_segments);
        m_mesh = m_builder.build();
        m_mesh.upload(ctx);
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "SphereGeometry"; }

private:
    float m_radius = 0.5f;
    int m_segments = 24;
};

// =============================================================================
// CylinderGeometry
// =============================================================================

/**
 * @brief Cylinder geometry generator
 *
 * Creates a cylinder with specified radius, height, and detail level.
 *
 * @par Example
 * @code
 * auto& cylinder = chain.add<CylinderGeometry>("cylinder")
 *     .radius(0.5f)
 *     .height(2.0f)
 *     .segments(24);
 * @endcode
 */
class CylinderGeometry : public GeometryOperator {
public:
    /// Set cylinder radius
    CylinderGeometry& radius(float r) {
        m_radius = r;
        return *this;
    }

    /// Set cylinder height
    CylinderGeometry& height(float h) {
        m_height = h;
        return *this;
    }

    /// Set number of segments (detail level)
    CylinderGeometry& segments(int s) {
        m_segments = s;
        return *this;
    }

    /// Enable flat shading
    CylinderGeometry& flatShading(bool enabled) {
        m_flatShading = enabled;
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        m_builder = MeshBuilder::cylinder(m_radius, m_height, m_segments);
        if (m_flatShading) {
            m_builder.computeFlatNormals();
        }
        m_mesh = m_builder.build();
        m_mesh.upload(ctx);
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "CylinderGeometry"; }

private:
    float m_radius = 0.5f;
    float m_height = 1.0f;
    int m_segments = 24;
    bool m_flatShading = false;
};

// =============================================================================
// ConeGeometry
// =============================================================================

/**
 * @brief Cone geometry generator
 *
 * Creates a cone with specified base radius, height, and detail level.
 *
 * @par Example
 * @code
 * auto& cone = chain.add<ConeGeometry>("cone")
 *     .radius(0.5f)
 *     .height(1.0f)
 *     .segments(24);
 * @endcode
 */
class ConeGeometry : public GeometryOperator {
public:
    /// Set cone base radius
    ConeGeometry& radius(float r) {
        m_radius = r;
        return *this;
    }

    /// Set cone height
    ConeGeometry& height(float h) {
        m_height = h;
        return *this;
    }

    /// Set number of segments (detail level)
    ConeGeometry& segments(int s) {
        m_segments = s;
        return *this;
    }

    /// Enable flat shading
    ConeGeometry& flatShading(bool enabled) {
        m_flatShading = enabled;
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        m_builder = MeshBuilder::cone(m_radius, m_height, m_segments);
        if (m_flatShading) {
            m_builder.computeFlatNormals();
        }
        m_mesh = m_builder.build();
        m_mesh.upload(ctx);
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "ConeGeometry"; }

private:
    float m_radius = 0.5f;
    float m_height = 1.0f;
    int m_segments = 24;
    bool m_flatShading = true;
};

// =============================================================================
// TorusGeometry
// =============================================================================

/**
 * @brief Torus (donut) geometry generator
 *
 * Creates a torus with specified outer/inner radii and detail levels.
 *
 * @par Example
 * @code
 * auto& torus = chain.add<TorusGeometry>("torus")
 *     .outerRadius(0.5f)
 *     .innerRadius(0.2f)
 *     .segments(32)
 *     .rings(16);
 * @endcode
 */
class TorusGeometry : public GeometryOperator {
public:
    /// Set outer radius (distance from center to tube center)
    TorusGeometry& outerRadius(float r) {
        m_outerRadius = r;
        return *this;
    }

    /// Set inner radius (tube radius)
    TorusGeometry& innerRadius(float r) {
        m_innerRadius = r;
        return *this;
    }

    /// Set number of segments around the ring
    TorusGeometry& segments(int s) {
        m_segments = s;
        return *this;
    }

    /// Set number of rings around the tube
    TorusGeometry& rings(int r) {
        m_rings = r;
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        m_builder = MeshBuilder::torus(m_outerRadius, m_innerRadius, m_segments, m_rings);
        m_mesh = m_builder.build();
        m_mesh.upload(ctx);
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "TorusGeometry"; }

private:
    float m_outerRadius = 0.5f;
    float m_innerRadius = 0.2f;
    int m_segments = 32;
    int m_rings = 16;
};

// =============================================================================
// PlaneGeometry
// =============================================================================

/**
 * @brief Plane geometry generator
 *
 * Creates a flat plane with optional subdivisions for displacement effects.
 *
 * @par Example
 * @code
 * auto& plane = chain.add<PlaneGeometry>("plane")
 *     .size(10.0f, 10.0f)
 *     .subdivisions(16, 16);
 * @endcode
 */
class PlaneGeometry : public GeometryOperator {
public:
    /// Set plane dimensions
    PlaneGeometry& size(float width, float height) {
        m_width = width;
        m_height = height;
        return *this;
    }

    /// Set number of subdivisions
    PlaneGeometry& subdivisions(int x, int y) {
        m_subdivisionsX = x;
        m_subdivisionsY = y;
        return *this;
    }

    /// Enable flat shading
    PlaneGeometry& flatShading(bool enabled) {
        m_flatShading = enabled;
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        m_builder = MeshBuilder::plane(m_width, m_height, m_subdivisionsX, m_subdivisionsY);
        if (m_flatShading) {
            m_builder.computeFlatNormals();
        }
        m_mesh = m_builder.build();
        m_mesh.upload(ctx);
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "PlaneGeometry"; }

private:
    float m_width = 1.0f;
    float m_height = 1.0f;
    int m_subdivisionsX = 1;
    int m_subdivisionsY = 1;
    bool m_flatShading = false;
};

} // namespace vivid::render3d
