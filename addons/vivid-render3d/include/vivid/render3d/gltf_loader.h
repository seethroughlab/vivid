#pragma once

/**
 * @file gltf_loader.h
 * @brief GLTF model loading operator
 *
 * Loads 3D models from GLTF/GLB files. Supports:
 * - Mesh geometry (vertices, normals, UVs, tangents)
 * - Multiple meshes/primitives (first mesh is used by default)
 * - Embedded and external textures
 *
 * Uses cgltf for parsing and stb_image for texture loading.
 */

#include <vivid/operator.h>
#include <vivid/render3d/mesh_operator.h>
#include <vivid/render3d/textured_material.h>
#include <vivid/param_registry.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <limits>

namespace vivid::render3d {

/// Axis-aligned bounding box
struct Bounds3D {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 size() const { return max - min; }
    float radius() const { return glm::length(size()) * 0.5f; }

    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }
};

/**
 * @brief Loads 3D models from GLTF/GLB files
 *
 * GLTFLoader is a MeshOperator that loads geometry from GLTF files.
 * It extracts vertices, normals, UVs, and optionally tangents.
 *
 * @par Basic Usage
 * @code
 * auto& model = chain.add<GLTFLoader>("model")
 *     .file("assets/models/helmet.glb");
 *
 * auto& scene = SceneComposer::create(chain, "scene");
 * scene.add(&model, glm::mat4(1.0f), glm::vec4(1.0f));
 *
 * chain.add<Render3D>("render")
 *     .input(&scene)
 *     .cameraInput(&camera)
 *     .lightInput(&sun);
 * @endcode
 *
 * @par With Textures
 * @code
 * auto& model = chain.add<GLTFLoader>("model")
 *     .file("assets/models/helmet.glb")
 *     .loadTextures(true);
 *
 * // Access material for textured rendering
 * auto* material = model.material();
 * if (material) {
 *     scene.entries().back().material = material;
 * }
 * @endcode
 */
class GLTFLoader : public MeshOperator, public ParamRegistry {
public:
    Param<float> scale{"scale", 1.0f, 0.001f, 100.0f};  ///< Model scale factor

    GLTFLoader();
    ~GLTFLoader() override;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set the GLTF/GLB file to load
     * @param path Path to the file (relative to project or absolute)
     */
    void file(const std::string& path);

    /**
     * @brief Select which mesh to load (for multi-mesh files)
     * @param index Mesh index (0 = first mesh, default)
     */
    void meshIndex(int index);

    /**
     * @brief Enable/disable texture loading
     * @param enabled True to load textures from GLTF material
     *
     * When enabled, creates a TexturedMaterial with base color,
     * normal, metallic-roughness, and other maps if present.
     */
    void loadTextures(bool enabled) {
        if (m_loadTextures != enabled) {
            m_loadTextures = enabled;
            markDirty();
        }
    }

    /**
     * @brief Compute tangents for normal mapping
     * @param enabled True to compute tangents (default: true if textures enabled)
     */
    void computeTangents(bool enabled) {
        if (m_computeTangents != enabled) {
            m_computeTangents = enabled;
            markDirty();
        }
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Output
    /// @{

    /**
     * @brief Get the loaded material (if textures were loaded)
     * @return Pointer to TexturedMaterial, or nullptr
     */
    TexturedMaterial* material() { return m_material.get(); }

    /**
     * @brief Override to expose material for SceneComposer
     */
    TexturedMaterial* outputMaterial() override { return m_material.get(); }

    /**
     * @brief Check if the model loaded successfully
     * @return True if geometry was loaded
     */
    bool isLoaded() const { return m_loaded; }

    /**
     * @brief Get any error message from loading
     * @return Error string, or empty if no error
     */
    const std::string& error() const { return m_error; }

    /**
     * @brief Get the bounding box of the loaded model
     * @return Axis-aligned bounding box
     */
    const Bounds3D& bounds() const { return m_bounds; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "GLTFLoader"; }

    /// @}

private:
    bool loadGLTF(Context& ctx);
    void computeMeshTangents();

    std::string m_filePath;
    std::string m_baseDir;      // Directory containing the GLTF file
    int m_meshIndex = -1;  // -1 = load all meshes
    bool m_loadTextures = false;
    bool m_computeTangents = true;
    bool m_loaded = false;
    bool m_needsLoad = false;
    std::string m_error;

    Bounds3D m_bounds;
    std::unique_ptr<TexturedMaterial> m_material;
};

} // namespace vivid::render3d
