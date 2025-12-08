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
#include <string>
#include <memory>

namespace vivid::render3d {

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
class GLTFLoader : public MeshOperator {
public:
    GLTFLoader();
    ~GLTFLoader() override;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set the GLTF/GLB file to load
     * @param path Path to the file (relative to project or absolute)
     * @return Reference to this operator for chaining
     */
    GLTFLoader& file(const std::string& path);

    /**
     * @brief Select which mesh to load (for multi-mesh files)
     * @param index Mesh index (0 = first mesh, default)
     * @return Reference to this operator for chaining
     */
    GLTFLoader& meshIndex(int index);

    /**
     * @brief Enable/disable texture loading
     * @param enabled True to load textures from GLTF material
     * @return Reference to this operator for chaining
     *
     * When enabled, creates a TexturedMaterial with base color,
     * normal, metallic-roughness, and other maps if present.
     */
    GLTFLoader& loadTextures(bool enabled);

    /**
     * @brief Scale the model uniformly
     * @param scale Scale factor (1.0 = original size)
     * @return Reference to this operator for chaining
     */
    GLTFLoader& scale(float scale);

    /**
     * @brief Compute tangents for normal mapping
     * @param enabled True to compute tangents (default: true if textures enabled)
     * @return Reference to this operator for chaining
     */
    GLTFLoader& computeTangents(bool enabled);

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

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "GLTFLoader"; }

    std::vector<vivid::ParamDecl> params() override {
        std::vector<vivid::ParamDecl> p;
        if (!m_filePath.empty()) {
            // Show just filename, not full path
            size_t lastSlash = m_filePath.find_last_of("/\\");
            std::string filename = (lastSlash != std::string::npos)
                ? m_filePath.substr(lastSlash + 1)
                : m_filePath;
            p.push_back({"file: " + filename, vivid::ParamType::String, 0, 0, {}});
        }
        if (m_loaded) {
            p.push_back({"verts", vivid::ParamType::Int, 0, 0, {static_cast<float>(m_mesh.vertexCount())}});
            p.push_back({"tris", vivid::ParamType::Int, 0, 0, {static_cast<float>(m_mesh.indexCount() / 3)}});
        }
        return p;
    }

    /// @}

private:
    bool loadGLTF(Context& ctx);
    void computeMeshTangents();

    std::string m_filePath;
    std::string m_baseDir;      // Directory containing the GLTF file
    int m_meshIndex = 0;
    float m_scale = 1.0f;
    bool m_loadTextures = false;
    bool m_computeTangents = true;
    bool m_loaded = false;
    bool m_needsLoad = false;
    std::string m_error;

    std::unique_ptr<TexturedMaterial> m_material;
};

} // namespace vivid::render3d
