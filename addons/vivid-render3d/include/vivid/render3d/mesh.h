#pragma once

#include <glm/glm.hpp>
#include <webgpu/webgpu.h>
#include <vector>
#include <cstdint>

namespace vivid {
class Context;
}

namespace vivid::render3d {

/// Vertex format for 3D meshes
struct Vertex3D {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color;

    Vertex3D() : position(0), normal(0, 1, 0), uv(0), color(1) {}

    Vertex3D(glm::vec3 pos)
        : position(pos), normal(0, 1, 0), uv(0), color(1) {}

    Vertex3D(glm::vec3 pos, glm::vec3 norm)
        : position(pos), normal(norm), uv(0), color(1) {}

    Vertex3D(glm::vec3 pos, glm::vec3 norm, glm::vec2 texcoord)
        : position(pos), normal(norm), uv(texcoord), color(1) {}

    Vertex3D(glm::vec3 pos, glm::vec3 norm, glm::vec2 texcoord, glm::vec4 col)
        : position(pos), normal(norm), uv(texcoord), color(col) {}
};

/// GPU mesh with vertices and indices
class Mesh {
public:
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    Mesh() = default;
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    /// Upload mesh data to GPU buffers
    void upload(Context& ctx);

    /// Release GPU resources
    void release();

    /// Check if GPU buffers are valid
    bool valid() const;

    /// Get vertex buffer for rendering
    WGPUBuffer vertexBuffer() const { return m_vertexBuffer; }

    /// Get index buffer for rendering
    WGPUBuffer indexBuffer() const { return m_indexBuffer; }

    /// Get index count for draw calls
    uint32_t indexCount() const { return static_cast<uint32_t>(indices.size()); }

    /// Get vertex count
    uint32_t vertexCount() const { return static_cast<uint32_t>(vertices.size()); }

private:
    WGPUBuffer m_vertexBuffer = nullptr;
    WGPUBuffer m_indexBuffer = nullptr;
};

} // namespace vivid::render3d
