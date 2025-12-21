#pragma once

/**
 * @file primitives.h
 * @brief Primitive mesh operators
 *
 * Provides GeometryOperator implementations for common 3D primitives:
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

#include <vivid/render3d/geometry_operator.h>
#include <vivid/render3d/mesh_builder.h>
#include <vivid/param.h>
#include <vivid/context.h>

namespace vivid::render3d {

// =============================================================================
// Box
// =============================================================================

/**
 * @brief Box/cube mesh generator
 *
 * Creates a box with specified dimensions. Always uses flat shading
 * since smooth shading doesn't make sense for a cube.
 *
 * @par Example
 * @code
 * auto& box = chain.add<Box>("box");
 * box.size(1.0f, 2.0f, 1.0f);
 * @endcode
 */
class Box : public GeometryOperator {
public:
    Box() {
        registerParam(m_width);
        registerParam(m_height);
        registerParam(m_depth);
    }

    /// Set box dimensions
    void size(float width, float height, float depth) {
        if (static_cast<float>(m_width) != width ||
            static_cast<float>(m_height) != height ||
            static_cast<float>(m_depth) != depth) {
            m_width = width;
            m_height = height;
            m_depth = depth;
            markDirty();
        }
    }

    /// Set uniform size (cube)
    void size(float s) { size(s, s, s); }

    void process(Context& ctx) override {
        if (needsCook()) {
            m_builder = MeshBuilder::box(m_width, m_height, m_depth);
            finalizeMesh(ctx, true);  // Always flat for boxes
        }
        updatePreview(ctx);  // Always update for rotation animation
    }

    std::string name() const override { return "Box"; }

private:
    Param<float> m_width{"width", 1.0f, 0.01f, 100.0f};
    Param<float> m_height{"height", 1.0f, 0.01f, 100.0f};
    Param<float> m_depth{"depth", 1.0f, 0.01f, 100.0f};
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
 * auto& sphere = chain.add<Sphere>("sphere");
 * sphere.radius(0.5f);
 * sphere.segments(32);
 * @endcode
 */
class Sphere : public GeometryOperator {
public:
    Sphere() {
        registerParam(m_radius);
        registerParam(m_segments);
    }

    /// Set sphere radius
    void radius(float r) {
        if (static_cast<float>(m_radius) != r) {
            m_radius = r;
            markDirty();
        }
    }

    /// Set number of segments (detail level)
    void segments(int s) {
        if (static_cast<int>(m_segments) != s) {
            m_segments = s;
            markDirty();
        }
    }

    /**
     * @brief Enable procedural noise displacement
     * @param amplitude Maximum displacement distance (default 0)
     * @param frequency Noise frequency/scale (default 1)
     * @param octaves Number of noise octaves (default 4)
     */
    void noiseDisplacement(float amplitude, float frequency = 1.0f, int octaves = 4) {
        if (m_noiseAmplitude != amplitude || m_noiseFrequency != frequency || m_noiseOctaves != octaves) {
            m_noiseAmplitude = amplitude;
            m_noiseFrequency = frequency;
            m_noiseOctaves = octaves;
            markDirty();
        }
    }

    /// Set noise time offset for animation
    void noiseTime(float t) {
        if (m_noiseTime != t) {
            m_noiseTime = t;
            markDirty();
        }
    }

    void process(Context& ctx) override {
        if (needsCook()) {
            m_builder = MeshBuilder::sphere(m_radius, m_segments);

            // Apply noise displacement if enabled
            if (m_noiseAmplitude > 0.0f) {
                m_builder.noiseDisplace(m_noiseAmplitude, m_noiseFrequency, m_noiseOctaves, m_noiseTime);
            }

            finalizeMesh(ctx);  // Uses m_flatShading from base
        }
        updatePreview(ctx);  // Always update for rotation animation
    }

    std::string name() const override { return "Sphere"; }

private:
    Param<float> m_radius{"radius", 0.5f, 0.01f, 100.0f};
    Param<int> m_segments{"segments", 24, 4, 128};

    // Noise displacement (not exposed as params - set via methods)
    float m_noiseAmplitude = 0.0f;
    float m_noiseFrequency = 1.0f;
    int m_noiseOctaves = 4;
    float m_noiseTime = 0.0f;
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
 * auto& cylinder = chain.add<Cylinder>("cylinder");
 * cylinder.radius(0.5f);
 * cylinder.height(2.0f);
 * cylinder.segments(24);
 * @endcode
 */
class Cylinder : public GeometryOperator {
public:
    Cylinder() {
        registerParam(m_radius);
        registerParam(m_height);
        registerParam(m_segments);
    }

    /// Set cylinder radius
    void radius(float r) {
        if (static_cast<float>(m_radius) != r) {
            m_radius = r;
            markDirty();
        }
    }

    /// Set cylinder height
    void height(float h) {
        if (static_cast<float>(m_height) != h) {
            m_height = h;
            markDirty();
        }
    }

    /// Set number of segments (detail level)
    void segments(int s) {
        if (static_cast<int>(m_segments) != s) {
            m_segments = s;
            markDirty();
        }
    }

    void process(Context& ctx) override {
        if (needsCook()) {
            m_builder = MeshBuilder::cylinder(m_radius, m_height, m_segments);
            finalizeMesh(ctx);  // Uses m_flatShading from base
        }
        updatePreview(ctx);  // Always update for rotation animation
    }

    std::string name() const override { return "Cylinder"; }

private:
    Param<float> m_radius{"radius", 0.5f, 0.01f, 100.0f};
    Param<float> m_height{"height", 1.0f, 0.01f, 100.0f};
    Param<int> m_segments{"segments", 24, 3, 128};
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
 * auto& cone = chain.add<Cone>("cone");
 * cone.radius(0.5f);
 * cone.height(1.0f);
 * cone.segments(24);
 * @endcode
 */
class Cone : public GeometryOperator {
public:
    Cone() {
        m_flatShading = true;  // Cones look better with flat shading by default
        registerParam(m_radius);
        registerParam(m_height);
        registerParam(m_segments);
    }

    /// Set cone base radius
    void radius(float r) {
        if (static_cast<float>(m_radius) != r) {
            m_radius = r;
            markDirty();
        }
    }

    /// Set cone height
    void height(float h) {
        if (static_cast<float>(m_height) != h) {
            m_height = h;
            markDirty();
        }
    }

    /// Set number of segments (detail level)
    void segments(int s) {
        if (static_cast<int>(m_segments) != s) {
            m_segments = s;
            markDirty();
        }
    }

    void process(Context& ctx) override {
        if (needsCook()) {
            m_builder = MeshBuilder::cone(m_radius, m_height, m_segments);
            finalizeMesh(ctx);
        }
        updatePreview(ctx);  // Always update for rotation animation
    }

    std::string name() const override { return "Cone"; }

private:
    Param<float> m_radius{"radius", 0.5f, 0.01f, 100.0f};
    Param<float> m_height{"height", 1.0f, 0.01f, 100.0f};
    Param<int> m_segments{"segments", 24, 3, 128};
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
 * auto& torus = chain.add<Torus>("torus");
 * torus.outerRadius(0.5f);
 * torus.innerRadius(0.2f);
 * torus.segments(32);
 * torus.rings(16);
 * @endcode
 */
class Torus : public GeometryOperator {
public:
    Torus() {
        registerParam(m_outerRadius);
        registerParam(m_innerRadius);
        registerParam(m_segments);
        registerParam(m_rings);
    }

    /// Set outer radius (distance from center to tube center)
    void outerRadius(float r) {
        if (static_cast<float>(m_outerRadius) != r) {
            m_outerRadius = r;
            markDirty();
        }
    }

    /// Set inner radius (tube radius)
    void innerRadius(float r) {
        if (static_cast<float>(m_innerRadius) != r) {
            m_innerRadius = r;
            markDirty();
        }
    }

    /// Set number of segments around the ring
    void segments(int s) {
        if (static_cast<int>(m_segments) != s) {
            m_segments = s;
            markDirty();
        }
    }

    /// Set number of rings around the tube
    void rings(int r) {
        if (static_cast<int>(m_rings) != r) {
            m_rings = r;
            markDirty();
        }
    }

    void process(Context& ctx) override {
        if (needsCook()) {
            m_builder = MeshBuilder::torus(m_outerRadius, m_innerRadius, m_segments, m_rings);
            finalizeMesh(ctx);
        }
        updatePreview(ctx);  // Always update for rotation animation
    }

    std::string name() const override { return "Torus"; }

private:
    Param<float> m_outerRadius{"outerRadius", 0.5f, 0.01f, 100.0f};
    Param<float> m_innerRadius{"innerRadius", 0.2f, 0.01f, 50.0f};
    Param<int> m_segments{"segments", 32, 3, 128};
    Param<int> m_rings{"rings", 16, 3, 128};
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
 * auto& plane = chain.add<Plane>("plane");
 * plane.size(10.0f, 10.0f);
 * plane.subdivisions(16, 16);
 * @endcode
 */
class Plane : public GeometryOperator {
public:
    Plane() {
        registerParam(m_width);
        registerParam(m_height);
        registerParam(m_subdivisionsX);
        registerParam(m_subdivisionsY);
    }

    /// Set plane dimensions
    void size(float width, float height) {
        if (static_cast<float>(m_width) != width ||
            static_cast<float>(m_height) != height) {
            m_width = width;
            m_height = height;
            markDirty();
        }
    }

    /// Set number of subdivisions
    void subdivisions(int x, int y) {
        if (static_cast<int>(m_subdivisionsX) != x ||
            static_cast<int>(m_subdivisionsY) != y) {
            m_subdivisionsX = x;
            m_subdivisionsY = y;
            markDirty();
        }
    }

    void process(Context& ctx) override {
        if (needsCook()) {
            m_builder = MeshBuilder::plane(m_width, m_height, m_subdivisionsX, m_subdivisionsY);
            finalizeMesh(ctx, true);  // Planes are always flat
        }
        updatePreview(ctx);  // Always update for rotation animation
    }

    std::string name() const override { return "Plane"; }

private:
    Param<float> m_width{"width", 1.0f, 0.01f, 1000.0f};
    Param<float> m_height{"height", 1.0f, 0.01f, 1000.0f};
    Param<int> m_subdivisionsX{"subdivisionsX", 1, 1, 256};
    Param<int> m_subdivisionsY{"subdivisionsY", 1, 1, 256};
};

} // namespace vivid::render3d
