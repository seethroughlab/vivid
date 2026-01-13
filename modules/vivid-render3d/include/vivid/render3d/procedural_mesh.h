#pragma once

/**
 * @file procedural_mesh.h
 * @brief Base class for procedural mesh generators
 *
 * ProceduralMesh provides a separation between mesh generation and rendering.
 * Subclasses generate geometry and instance data; Render3D handles all rendering
 * including shadow maps and lighting.
 */

#include <vivid/operator.h>
#include <vivid/render3d/mesh.h>
#include <glm/glm.hpp>
#include <webgpu/webgpu.h>
#include <vector>

namespace vivid::render3d {

/**
 * @brief Instance data for GPU instancing
 *
 * Each instance represents one copy of the procedural mesh with its own
 * transform, scale variation, and phase offset for animations.
 */
struct ProceduralInstance {
    glm::mat4 transform;    ///< World transform for this instance
    glm::vec4 variation;    ///< xyz = scale variation, w = animation phase
    glm::vec4 color;        ///< RGBA color (may be unused if using gradient)
};

/**
 * @brief Wind animation parameters for vertex shaders
 *
 * These parameters control GPU-based wind animation. The renderer uses
 * these values in a unified wind shader for all procedural meshes.
 */
struct WindParams {
    float strength = 0.0f;          ///< Wind strength (displacement amount)
    float speed = 1.0f;             ///< Animation speed
    glm::vec2 direction{1.0f, 0.3f}; ///< Wind direction (XZ plane)
    float stemCurve = 0.0f;         ///< Vertical droop factor
    float stemLength = 0.0f;        ///< Stem length (for animation scaling)
};

/**
 * @brief Base class for procedural mesh generators
 *
 * ProceduralMesh separates mesh generation from rendering. Subclasses
 * implement geometry generation and instance placement; Render3D handles
 * rendering with proper shadow maps, lighting, and depth testing.
 *
 * This architecture enables:
 * - Unified shadow maps across all scene geometry
 * - Proper depth testing between procedural and scene meshes
 * - Single lighting pass for consistent shading
 * - GPU-based wind animation in a unified shader
 *
 * @par Example
 * @code
 * auto& foliage = chain.add<FoliageMesh>("foliage");
 * foliage.setPlantType(FoliageMesh::PlantType::Fern);
 * foliage.frondCount = 200;
 *
 * auto& render = chain.add<Render3D>("render");
 * render.setInput(&scene);
 * render.addProceduralMesh(&foliage);
 * @endcode
 */
class ProceduralMesh : public Operator {
public:
    ProceduralMesh() = default;
    virtual ~ProceduralMesh() = default;

    // =========================================================================
    /// @name Mesh Data
    /// @{

    /**
     * @brief Get the procedural mesh geometry
     *
     * Returns the mesh that will be instanced. The mesh is generated
     * CPU-side and uploaded to GPU by Render3D when dirty.
     *
     * @return Reference to the procedural mesh
     */
    virtual const Mesh& getMesh() const = 0;

    /**
     * @brief Get the instance transforms and variations
     *
     * Each instance represents one copy of the mesh with its own
     * world transform, scale variation, and animation phase.
     *
     * @return Vector of instance data
     */
    virtual const std::vector<ProceduralInstance>& getInstances() const = 0;

    /// @}
    // =========================================================================
    /// @name Animation
    /// @{

    /**
     * @brief Get wind animation parameters
     *
     * The renderer uses these parameters for GPU-based vertex animation.
     * Override to enable wind effects on this mesh.
     *
     * @return Wind parameters (default is no wind)
     */
    virtual WindParams getWindParams() const { return {}; }

    /// @}
    // =========================================================================
    /// @name Material
    /// @{

    /**
     * @brief Get base color (at mesh root/base)
     *
     * Used for gradient coloring. For vegetation, this is typically
     * the darker color at the stem/blade base.
     */
    virtual glm::vec3 getBaseColor() const = 0;

    /**
     * @brief Get tip color (at mesh tip)
     *
     * Used for gradient coloring. For vegetation, this is typically
     * the lighter color at the leaf/blade tip.
     */
    virtual glm::vec3 getTipColor() const = 0;

    /// @}
    // =========================================================================
    /// @name Leaf Texture (Optional)
    /// @{

    /**
     * @brief Check if this mesh has a leaf texture
     *
     * When true, the renderer will sample from getLeafTextureView() for
     * billboard fragments and apply alpha discard.
     */
    virtual bool hasLeafTexture() const { return false; }

    /**
     * @brief Get the leaf texture view for billboard rendering
     *
     * Only called if hasLeafTexture() returns true.
     * The texture should have an alpha channel for cutout.
     *
     * @return Texture view, or nullptr if no texture
     */
    virtual WGPUTextureView getLeafTextureView() const { return nullptr; }

    /**
     * @brief Get the leaf texture sampler
     *
     * Only called if hasLeafTexture() returns true.
     *
     * @return Sampler, or nullptr to use default
     */
    virtual WGPUSampler getLeafSampler() const { return nullptr; }

    /// @}
    // =========================================================================
    /// @name Shadow Settings
    /// @{

    /// Whether this mesh casts shadows
    bool castShadow = true;

    /// Whether this mesh receives shadows
    bool receiveShadow = true;

    /// @}
    // =========================================================================
    /// @name Dirty Tracking
    /// @{

    /**
     * @brief Check if mesh needs to be re-uploaded
     *
     * Render3D checks this to avoid unnecessary GPU uploads.
     */
    bool meshDirty() const { return m_meshDirty; }

    /**
     * @brief Check if instances need to be re-uploaded
     *
     * Render3D checks this to avoid unnecessary GPU uploads.
     */
    bool instancesDirty() const { return m_instancesDirty; }

    /**
     * @brief Clear dirty flags after GPU upload
     *
     * Called by Render3D after uploading buffers.
     */
    void clearDirtyFlags() {
        m_meshDirty = false;
        m_instancesDirty = false;
    }

    /// @}
    // =========================================================================
    /// @name Operator Interface
    /// @{

    /**
     * @brief ProceduralMesh outputs nothing directly
     *
     * The mesh data is accessed via getMesh() and getInstances().
     * Render3D handles actual rendering. No texture output.
     */
    WGPUTextureView outputView() const override { return nullptr; }
    WGPUTexture outputTexture() const override { return nullptr; }

    /// @}

protected:
    bool m_meshDirty = true;       ///< Mesh geometry needs re-upload
    bool m_instancesDirty = true;  ///< Instance data needs re-upload
};

} // namespace vivid::render3d
