#pragma once
#include <vivid/graphics3d.h>
#include <webgpu/webgpu.h>
#include <vector>
#include <cstdint>

#ifdef VIVID_USE_DILIGENT
#include "diligent_pbr.h"
#endif

namespace vivid {

// Forward declarations
class Renderer;
class DiligentPBR;

/**
 * @brief 3D mesh with vertex and index buffers.
 *
 * Create meshes using Mesh::create() or primitive generators.
 * Meshes are rendered using Mesh::draw() within a render pass.
 */
class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    // Non-copyable, movable
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    /**
     * @brief Create a mesh from vertex and index data.
     * @param renderer The renderer to create GPU resources with.
     * @param vertices Vector of Vertex3D data.
     * @param indices Vector of triangle indices (must be multiple of 3).
     * @return true if creation succeeded.
     */
    bool create(Renderer& renderer,
                const std::vector<Vertex3D>& vertices,
                const std::vector<uint32_t>& indices);

    /**
     * @brief Release GPU resources.
     */
    void destroy();

    /**
     * @brief Draw this mesh in the current render pass.
     * @param encoder Active render pass encoder.
     * @param instanceCount Number of instances to draw (default 1).
     */
    void draw(WGPURenderPassEncoder encoder, uint32_t instanceCount = 1) const;

    /**
     * @brief Check if this mesh has valid GPU resources.
     */
    bool valid() const { return vertexBuffer_ != nullptr && indexBuffer_ != nullptr; }

    // Accessors
    uint32_t vertexCount() const { return vertexCount_; }
    uint32_t indexCount() const { return indexCount_; }
    const BoundingBox& bounds() const { return bounds_; }
    WGPUBuffer vertexBuffer() const { return vertexBuffer_; }
    WGPUBuffer indexBuffer() const { return indexBuffer_; }

    /**
     * @brief Get the vertex buffer layout for pipeline creation.
     * @return Vertex buffer layout descriptor.
     */
    static WGPUVertexBufferLayout getVertexLayout();

#ifdef VIVID_USE_DILIGENT
    /**
     * @brief Create Diligent mesh buffers from the same vertex/index data.
     * Call this after create() to enable Diligent rendering.
     */
    bool createDiligentMesh(DiligentPBR& pbr,
                            const std::vector<Vertex3D>& vertices,
                            const std::vector<uint32_t>& indices);

    /**
     * @brief Check if Diligent mesh data is available.
     */
    bool hasDiligentMesh() const { return diligentMesh_.vertexBuffer != nullptr; }

    /**
     * @brief Get Diligent mesh data for rendering.
     */
    const DiligentMeshData& diligentMesh() const { return diligentMesh_; }
#endif

private:
    WGPUBuffer vertexBuffer_ = nullptr;
    WGPUBuffer indexBuffer_ = nullptr;
    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;
    BoundingBox bounds_;

#ifdef VIVID_USE_DILIGENT
    DiligentMeshData diligentMesh_;
#endif
};

// Primitive generators are declared in graphics3d.h and implemented in mesh.cpp

} // namespace vivid
