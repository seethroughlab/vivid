#pragma once

/**
 * @file geometry_operator.h
 * @brief Base class for procedural geometry primitives
 *
 * GeometryOperator extends MeshOperator with ParamRegistry integration
 * and default implementations for common boilerplate. Use this base class
 * for primitives like Box, Sphere, Cylinder, etc.
 *
 * Key benefits over raw MeshOperator:
 * - Default init() (empty) and cleanup() (releases mesh)
 * - Automatic params()/getParam()/setParam() via ParamRegistry
 * - Common shading options (flatShading, computeTangents)
 *
 * @par Example Implementation
 * @code
 * class Box : public GeometryOperator {
 * public:
 *     Box() {
 *         registerParam(m_width);
 *         registerParam(m_height);
 *         registerParam(m_depth);
 *     }
 *
 *     void size(float w, float h, float d) {
 *         if (m_width != w || m_height != h || m_depth != d) {
 *             m_width = w; m_height = h; m_depth = d; markDirty();
 *         }
 *     }
 *
 *     void process(Context& ctx) override {
 *         if (!needsCook()) return;
 *         m_builder = MeshBuilder::box(m_width, m_height, m_depth);
 *         finalizeMesh(ctx);  // Handles normals, tangents, build, upload
 *     }
 *
 *     std::string name() const override { return "Box"; }
 *
 * private:
 *     Param<float> m_width{"width", 1.0f, 0.01f, 100.0f};
 *     Param<float> m_height{"height", 1.0f, 0.01f, 100.0f};
 *     Param<float> m_depth{"depth", 1.0f, 0.01f, 100.0f};
 * };
 * @endcode
 */

#include <vivid/render3d/mesh_operator.h>
#include <vivid/param_registry.h>
#include <vivid/param.h>

namespace vivid::render3d {

/**
 * @brief Base class for procedural geometry primitives
 *
 * Provides common infrastructure for geometry generators:
 * - ParamRegistry integration for automatic parameter handling
 * - Default init/cleanup implementations
 * - Common shading controls (flat shading, tangents)
 * - finalizeMesh() helper for the build/upload cycle
 */
class GeometryOperator : public MeshOperator, public ParamRegistry {
public:
    virtual ~GeometryOperator() = default;

    // -------------------------------------------------------------------------
    /// @name Lifecycle (defaults provided)
    /// @{

    /**
     * @brief Default empty initialization
     *
     * Override if you need to create additional GPU resources.
     */
    void init(Context& ctx) override {}

    /**
     * @brief Default cleanup releases the mesh
     *
     * Override if you have additional resources to release.
     */
    void cleanup() override {
        m_mesh.release();
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Parameter Introspection
    /// @{

    /**
     * @brief Get parameter declarations from registry
     * @return Vector of ParamDecl from registered parameters
     */
    std::vector<ParamDecl> params() override {
        return registeredParams();
    }

    /**
     * @brief Get parameter value from registry
     * @param name Parameter name
     * @param out Output array for value
     * @return True if parameter found
     */
    bool getParam(const std::string& name, float out[4]) override {
        return getRegisteredParam(name, out);
    }

    /**
     * @brief Set parameter value in registry
     * @param name Parameter name
     * @param value Input array with value
     * @return True if parameter was set
     *
     * Automatically calls markDirty() when a parameter changes.
     */
    bool setParam(const std::string& name, const float value[4]) override {
        if (setRegisteredParam(name, value)) {
            markDirty();
            return true;
        }
        return false;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Common Shading Options
    /// @{

    /**
     * @brief Enable flat shading (faceted look)
     * @param enabled True for flat normals, false for smooth
     */
    void flatShading(bool enabled) {
        if (m_flatShading != enabled) {
            m_flatShading = enabled;
            markDirty();
        }
    }

    /**
     * @brief Enable tangent computation (required for normal mapping)
     */
    void computeTangents() {
        if (!m_computeTangents) {
            m_computeTangents = true;
            markDirty();
        }
    }

    /// @}

protected:
    // -------------------------------------------------------------------------
    /// @name Mesh Finalization Helper
    /// @{

    /**
     * @brief Finalize mesh after geometry generation
     * @param ctx Runtime context for GPU upload
     * @param forceFlat Force flat normals regardless of m_flatShading
     *
     * Call this at the end of process() after setting m_builder.
     * Handles normal computation, tangent computation, build, and upload.
     *
     * @code
     * void process(Context& ctx) override {
     *     if (!needsCook()) return;
     *     m_builder = MeshBuilder::box(m_width, m_height, m_depth);
     *     finalizeMesh(ctx);
     * }
     * @endcode
     */
    void finalizeMesh(Context& ctx, bool forceFlat = false) {
        if (forceFlat || m_flatShading) {
            m_builder.computeFlatNormals();
        } else {
            m_builder.computeNormals();
        }

        if (m_computeTangents) {
            m_builder.computeTangents();
        }

        m_mesh = m_builder.build();
        m_mesh.upload(ctx);

        didCook();
    }

    /// @}

    bool m_flatShading = false;      ///< Use flat normals (faceted look)
    bool m_computeTangents = false;  ///< Compute tangents for normal mapping
};

} // namespace vivid::render3d
