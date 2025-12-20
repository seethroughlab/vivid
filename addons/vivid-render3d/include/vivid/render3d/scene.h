#pragma once

#include <vivid/render3d/mesh.h>
#include <glm/glm.hpp>
#include <vector>

namespace vivid::render3d {

// Forward declaration
class TexturedMaterial;

/// A single object in the scene
struct SceneObject {
    Mesh* mesh = nullptr;
    glm::mat4 transform = glm::mat4(1.0f);
    glm::vec4 color = glm::vec4(1.0f);
    TexturedMaterial* material = nullptr;  // Optional per-object material
    bool castShadow = true;  // Whether this object casts shadows

    SceneObject() = default;

    SceneObject(Mesh* m)
        : mesh(m), transform(glm::mat4(1.0f)), color(glm::vec4(1.0f)), material(nullptr), castShadow(true) {}

    SceneObject(Mesh* m, const glm::mat4& t)
        : mesh(m), transform(t), color(glm::vec4(1.0f)), material(nullptr), castShadow(true) {}

    SceneObject(Mesh* m, const glm::mat4& t, const glm::vec4& c)
        : mesh(m), transform(t), color(c), material(nullptr), castShadow(true) {}

    SceneObject(Mesh* m, const glm::mat4& t, const glm::vec4& c, TexturedMaterial* mat)
        : mesh(m), transform(t), color(c), material(mat), castShadow(true) {}
};

/// Container for multiple meshes with transforms
class Scene {
public:
    Scene() = default;

    // -------------------------------------------------------------------------
    /// @name Adding Objects
    /// @{

    /// Add a mesh with identity transform
    /// @deprecated Use SceneComposer::addMesh() for chain visualizer integration
    [[deprecated("Use SceneComposer::addMesh() for chain visualizer integration")]]
    Scene& add(Mesh& mesh);

    /// Add a mesh with a transform
    /// @deprecated Use SceneComposer::addMesh() for chain visualizer integration
    [[deprecated("Use SceneComposer::addMesh() for chain visualizer integration")]]
    Scene& add(Mesh& mesh, const glm::mat4& transform);

    /// Add a mesh with transform and color
    /// @deprecated Use SceneComposer::addMesh() for chain visualizer integration
    [[deprecated("Use SceneComposer::addMesh() for chain visualizer integration")]]
    Scene& add(Mesh& mesh, const glm::mat4& transform, const glm::vec4& color);

    /// Add a pre-configured SceneObject
    /// @deprecated Use SceneComposer::addMesh() for chain visualizer integration
    [[deprecated("Use SceneComposer::addMesh() for chain visualizer integration")]]
    Scene& add(const SceneObject& object);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Access
    /// @{

    /// Get all objects in the scene
    const std::vector<SceneObject>& objects() const { return m_objects; }

    /// Get mutable access to objects
    std::vector<SceneObject>& objects() { return m_objects; }

    /// Get number of objects
    size_t objectCount() const { return m_objects.size(); }

    /// Check if scene is empty
    bool empty() const { return m_objects.empty(); }

    /// Access object by index
    SceneObject& operator[](size_t index) { return m_objects[index]; }
    const SceneObject& operator[](size_t index) const { return m_objects[index]; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Management
    /// @{

    /// Clear all objects from the scene
    void clear();

    /// @}

private:
    std::vector<SceneObject> m_objects;
};

} // namespace vivid::render3d
