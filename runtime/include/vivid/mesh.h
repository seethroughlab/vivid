#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "EngineFactoryVk.h"
#include "Buffer.h"
#include "RefCntAutoPtr.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vivid {

using namespace Diligent;

// 3D Vertex format - position, normal, UV, tangent
// NOTE: DiligentFX expects tangent as float3 (ATTRIB7), bitangent is computed in shader
struct Vertex3D {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 tangent;

    Vertex3D() = default;
    Vertex3D(const glm::vec3& pos, const glm::vec3& norm, const glm::vec2& texCoord)
        : position(pos), normal(norm), uv(texCoord), tangent(0.0f) {}
    Vertex3D(const glm::vec3& pos, const glm::vec3& norm, const glm::vec2& texCoord, const glm::vec3& tan)
        : position(pos), normal(norm), uv(texCoord), tangent(tan) {}
};

// Axis-aligned bounding box
struct BoundingBox {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 size() const { return max - min; }
    float radius() const { return glm::length(size()) * 0.5f; }
};

// Mesh data on GPU
struct Mesh {
    RefCntAutoPtr<IBuffer> vertexBuffer;
    RefCntAutoPtr<IBuffer> indexBuffer;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    BoundingBox bounds;

    bool isValid() const { return vertexBuffer != nullptr; }
    operator bool() const { return isValid(); }
};

// CPU-side mesh data for building meshes
struct MeshData {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    BoundingBox bounds;

    void calculateBounds() {
        bounds = BoundingBox{};
        for (const auto& v : vertices) {
            bounds.expand(v.position);
        }
    }

    void calculateNormals();
    void calculateTangents();
};

// Mesh utilities for creating and loading meshes
class MeshUtils {
public:
    MeshUtils(IRenderDevice* device);
    ~MeshUtils();

    // Non-copyable
    MeshUtils(const MeshUtils&) = delete;
    MeshUtils& operator=(const MeshUtils&) = delete;

    // Create mesh from CPU data
    Mesh createFromData(const MeshData& data, const std::string& name = "Mesh");

    // Primitive generators
    Mesh createCube(float size = 1.0f);
    Mesh createSphere(float radius = 0.5f, uint32_t segments = 32, uint32_t rings = 16);
    Mesh createPlane(float width = 1.0f, float height = 1.0f, uint32_t subdivisionsX = 1, uint32_t subdivisionsY = 1);
    Mesh createCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);
    Mesh createCone(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);
    Mesh createTorus(float outerRadius = 0.5f, float innerRadius = 0.2f, uint32_t segments = 32, uint32_t rings = 16);

    // Get the last error message
    const std::string& getLastError() const { return m_lastError; }

private:
    IRenderDevice* m_device;
    std::string m_lastError;

    // Helper to upload mesh data to GPU
    Mesh uploadMesh(const MeshData& data, const std::string& name);
};

// Per-instance data for instanced rendering
struct Instance3D {
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};

} // namespace vivid
