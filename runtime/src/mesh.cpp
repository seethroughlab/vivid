#include "vivid/mesh.h"

#include <iostream>
#include <cmath>

namespace vivid {

using namespace Diligent;

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;

void MeshData::calculateNormals() {
    // Reset normals
    for (auto& v : vertices) {
        v.normal = glm::vec3(0.0f);
    }

    // Accumulate face normals
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        glm::vec3 v0 = vertices[i0].position;
        glm::vec3 v1 = vertices[i1].position;
        glm::vec3 v2 = vertices[i2].position;

        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 normal = glm::cross(edge1, edge2);

        vertices[i0].normal += normal;
        vertices[i1].normal += normal;
        vertices[i2].normal += normal;
    }

    // Normalize
    for (auto& v : vertices) {
        if (glm::length(v.normal) > 0.0001f) {
            v.normal = glm::normalize(v.normal);
        }
    }
}

void MeshData::calculateTangents() {
    // Reset tangents
    for (auto& v : vertices) {
        v.tangent = glm::vec4(0.0f);
    }

    std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        const glm::vec3& v0 = vertices[i0].position;
        const glm::vec3& v1 = vertices[i1].position;
        const glm::vec3& v2 = vertices[i2].position;

        const glm::vec2& uv0 = vertices[i0].uv;
        const glm::vec2& uv1 = vertices[i1].uv;
        const glm::vec2& uv2 = vertices[i2].uv;

        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;

        glm::vec2 deltaUV1 = uv1 - uv0;
        glm::vec2 deltaUV2 = uv2 - uv0;

        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y + 0.0001f);

        glm::vec3 tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        glm::vec3 bitangent;
        bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

        tan1[i0] += tangent;
        tan1[i1] += tangent;
        tan1[i2] += tangent;

        tan2[i0] += bitangent;
        tan2[i1] += bitangent;
        tan2[i2] += bitangent;
    }

    // Gram-Schmidt orthogonalize and calculate handedness
    for (size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec3& n = vertices[i].normal;
        const glm::vec3& t = tan1[i];

        glm::vec3 tangent = glm::normalize(t - n * glm::dot(n, t));
        float handedness = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;

        vertices[i].tangent = glm::vec4(tangent, handedness);
    }
}

MeshUtils::MeshUtils(IRenderDevice* device)
    : m_device(device) {
}

MeshUtils::~MeshUtils() = default;

Mesh MeshUtils::uploadMesh(const MeshData& data, const std::string& name) {
    Mesh mesh;

    if (data.vertices.empty()) {
        m_lastError = "No vertices in mesh data";
        return mesh;
    }

    // Create vertex buffer
    BufferDesc vbDesc;
    vbDesc.Name = (name + " VB").c_str();
    vbDesc.Size = static_cast<Uint64>(data.vertices.size() * sizeof(Vertex3D));
    vbDesc.BindFlags = BIND_VERTEX_BUFFER;
    vbDesc.Usage = USAGE_IMMUTABLE;

    BufferData vbData;
    vbData.pData = data.vertices.data();
    vbData.DataSize = vbDesc.Size;

    m_device->CreateBuffer(vbDesc, &vbData, &mesh.vertexBuffer);

    if (!mesh.vertexBuffer) {
        m_lastError = "Failed to create vertex buffer for: " + name;
        return mesh;
    }

    // Create index buffer if we have indices
    if (!data.indices.empty()) {
        BufferDesc ibDesc;
        ibDesc.Name = (name + " IB").c_str();
        ibDesc.Size = static_cast<Uint64>(data.indices.size() * sizeof(uint32_t));
        ibDesc.BindFlags = BIND_INDEX_BUFFER;
        ibDesc.Usage = USAGE_IMMUTABLE;

        BufferData ibData;
        ibData.pData = data.indices.data();
        ibData.DataSize = ibDesc.Size;

        m_device->CreateBuffer(ibDesc, &ibData, &mesh.indexBuffer);
    }

    mesh.vertexCount = static_cast<uint32_t>(data.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(data.indices.size());
    mesh.bounds = data.bounds;

    std::cout << "[MeshUtils] Created mesh: " << name
              << " (" << mesh.vertexCount << " vertices, " << mesh.indexCount << " indices)" << std::endl;

    return mesh;
}

Mesh MeshUtils::createFromData(const MeshData& data, const std::string& name) {
    return uploadMesh(data, name);
}

Mesh MeshUtils::createCube(float size) {
    float h = size * 0.5f;

    MeshData data;

    // Each face has 4 vertices with proper normals for flat shading
    // Front face (Z+)
    data.vertices.push_back({{-h, -h,  h}, { 0,  0,  1}, {0, 1}});
    data.vertices.push_back({{ h, -h,  h}, { 0,  0,  1}, {1, 1}});
    data.vertices.push_back({{ h,  h,  h}, { 0,  0,  1}, {1, 0}});
    data.vertices.push_back({{-h,  h,  h}, { 0,  0,  1}, {0, 0}});

    // Back face (Z-)
    data.vertices.push_back({{ h, -h, -h}, { 0,  0, -1}, {0, 1}});
    data.vertices.push_back({{-h, -h, -h}, { 0,  0, -1}, {1, 1}});
    data.vertices.push_back({{-h,  h, -h}, { 0,  0, -1}, {1, 0}});
    data.vertices.push_back({{ h,  h, -h}, { 0,  0, -1}, {0, 0}});

    // Top face (Y+)
    data.vertices.push_back({{-h,  h,  h}, { 0,  1,  0}, {0, 1}});
    data.vertices.push_back({{ h,  h,  h}, { 0,  1,  0}, {1, 1}});
    data.vertices.push_back({{ h,  h, -h}, { 0,  1,  0}, {1, 0}});
    data.vertices.push_back({{-h,  h, -h}, { 0,  1,  0}, {0, 0}});

    // Bottom face (Y-)
    data.vertices.push_back({{-h, -h, -h}, { 0, -1,  0}, {0, 1}});
    data.vertices.push_back({{ h, -h, -h}, { 0, -1,  0}, {1, 1}});
    data.vertices.push_back({{ h, -h,  h}, { 0, -1,  0}, {1, 0}});
    data.vertices.push_back({{-h, -h,  h}, { 0, -1,  0}, {0, 0}});

    // Right face (X+)
    data.vertices.push_back({{ h, -h,  h}, { 1,  0,  0}, {0, 1}});
    data.vertices.push_back({{ h, -h, -h}, { 1,  0,  0}, {1, 1}});
    data.vertices.push_back({{ h,  h, -h}, { 1,  0,  0}, {1, 0}});
    data.vertices.push_back({{ h,  h,  h}, { 1,  0,  0}, {0, 0}});

    // Left face (X-)
    data.vertices.push_back({{-h, -h, -h}, {-1,  0,  0}, {0, 1}});
    data.vertices.push_back({{-h, -h,  h}, {-1,  0,  0}, {1, 1}});
    data.vertices.push_back({{-h,  h,  h}, {-1,  0,  0}, {1, 0}});
    data.vertices.push_back({{-h,  h, -h}, {-1,  0,  0}, {0, 0}});

    // Indices (two triangles per face)
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        data.indices.push_back(base + 0);
        data.indices.push_back(base + 1);
        data.indices.push_back(base + 2);
        data.indices.push_back(base + 0);
        data.indices.push_back(base + 2);
        data.indices.push_back(base + 3);
    }

    data.calculateBounds();
    data.calculateTangents();

    return uploadMesh(data, "Cube");
}

Mesh MeshUtils::createSphere(float radius, uint32_t segments, uint32_t rings) {
    MeshData data;

    for (uint32_t ring = 0; ring <= rings; ++ring) {
        float phi = PI * float(ring) / float(rings);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);

        for (uint32_t seg = 0; seg <= segments; ++seg) {
            float theta = TWO_PI * float(seg) / float(segments);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            glm::vec3 normal(sinPhi * cosTheta, cosPhi, sinPhi * sinTheta);
            glm::vec3 position = normal * radius;
            glm::vec2 uv(float(seg) / float(segments), float(ring) / float(rings));

            data.vertices.push_back({position, normal, uv});
        }
    }

    // Indices
    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t seg = 0; seg < segments; ++seg) {
            uint32_t current = ring * (segments + 1) + seg;
            uint32_t next = current + segments + 1;

            data.indices.push_back(current);
            data.indices.push_back(next);
            data.indices.push_back(current + 1);

            data.indices.push_back(current + 1);
            data.indices.push_back(next);
            data.indices.push_back(next + 1);
        }
    }

    data.calculateBounds();
    data.calculateTangents();

    return uploadMesh(data, "Sphere");
}

Mesh MeshUtils::createPlane(float width, float height, uint32_t subdivisionsX, uint32_t subdivisionsY) {
    MeshData data;

    float halfW = width * 0.5f;
    float halfH = height * 0.5f;

    for (uint32_t y = 0; y <= subdivisionsY; ++y) {
        for (uint32_t x = 0; x <= subdivisionsX; ++x) {
            float u = float(x) / float(subdivisionsX);
            float v = float(y) / float(subdivisionsY);

            glm::vec3 position(-halfW + u * width, 0.0f, -halfH + v * height);
            glm::vec3 normal(0.0f, 1.0f, 0.0f);
            glm::vec2 uv(u, v);

            data.vertices.push_back({position, normal, uv});
        }
    }

    // Indices
    for (uint32_t y = 0; y < subdivisionsY; ++y) {
        for (uint32_t x = 0; x < subdivisionsX; ++x) {
            uint32_t current = y * (subdivisionsX + 1) + x;
            uint32_t next = current + subdivisionsX + 1;

            data.indices.push_back(current);
            data.indices.push_back(next);
            data.indices.push_back(current + 1);

            data.indices.push_back(current + 1);
            data.indices.push_back(next);
            data.indices.push_back(next + 1);
        }
    }

    data.calculateBounds();
    data.calculateTangents();

    return uploadMesh(data, "Plane");
}

Mesh MeshUtils::createCylinder(float radius, float height, uint32_t segments) {
    MeshData data;

    float halfH = height * 0.5f;

    // Side vertices
    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = TWO_PI * float(i) / float(segments);
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        glm::vec3 normal(cosT, 0.0f, sinT);

        // Bottom
        data.vertices.push_back({{cosT * radius, -halfH, sinT * radius}, normal, {float(i) / segments, 1.0f}});
        // Top
        data.vertices.push_back({{cosT * radius,  halfH, sinT * radius}, normal, {float(i) / segments, 0.0f}});
    }

    // Side indices
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t base = i * 2;
        data.indices.push_back(base);
        data.indices.push_back(base + 2);
        data.indices.push_back(base + 1);

        data.indices.push_back(base + 1);
        data.indices.push_back(base + 2);
        data.indices.push_back(base + 3);
    }

    // Top cap
    uint32_t topCenter = static_cast<uint32_t>(data.vertices.size());
    data.vertices.push_back({{0.0f, halfH, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}});

    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = TWO_PI * float(i) / float(segments);
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);
        data.vertices.push_back({{cosT * radius, halfH, sinT * radius}, {0.0f, 1.0f, 0.0f}, {cosT * 0.5f + 0.5f, sinT * 0.5f + 0.5f}});
    }

    for (uint32_t i = 0; i < segments; ++i) {
        data.indices.push_back(topCenter);
        data.indices.push_back(topCenter + 1 + i);
        data.indices.push_back(topCenter + 2 + i);
    }

    // Bottom cap
    uint32_t bottomCenter = static_cast<uint32_t>(data.vertices.size());
    data.vertices.push_back({{0.0f, -halfH, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f}});

    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = TWO_PI * float(i) / float(segments);
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);
        data.vertices.push_back({{cosT * radius, -halfH, sinT * radius}, {0.0f, -1.0f, 0.0f}, {cosT * 0.5f + 0.5f, sinT * 0.5f + 0.5f}});
    }

    for (uint32_t i = 0; i < segments; ++i) {
        data.indices.push_back(bottomCenter);
        data.indices.push_back(bottomCenter + 2 + i);
        data.indices.push_back(bottomCenter + 1 + i);
    }

    data.calculateBounds();
    data.calculateTangents();

    return uploadMesh(data, "Cylinder");
}

Mesh MeshUtils::createCone(float radius, float height, uint32_t segments) {
    MeshData data;

    float halfH = height * 0.5f;
    float slope = radius / height;

    // Apex
    uint32_t apexIndex = 0;
    data.vertices.push_back({{0.0f, halfH, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.0f}});

    // Base ring with normals pointing outward
    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = TWO_PI * float(i) / float(segments);
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        glm::vec3 normal = glm::normalize(glm::vec3(cosT, slope, sinT));
        data.vertices.push_back({{cosT * radius, -halfH, sinT * radius}, normal, {float(i) / segments, 1.0f}});
    }

    // Side triangles
    for (uint32_t i = 0; i < segments; ++i) {
        data.indices.push_back(apexIndex);
        data.indices.push_back(1 + i);
        data.indices.push_back(2 + i);
    }

    // Bottom cap
    uint32_t bottomCenter = static_cast<uint32_t>(data.vertices.size());
    data.vertices.push_back({{0.0f, -halfH, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f}});

    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = TWO_PI * float(i) / float(segments);
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);
        data.vertices.push_back({{cosT * radius, -halfH, sinT * radius}, {0.0f, -1.0f, 0.0f}, {cosT * 0.5f + 0.5f, sinT * 0.5f + 0.5f}});
    }

    for (uint32_t i = 0; i < segments; ++i) {
        data.indices.push_back(bottomCenter);
        data.indices.push_back(bottomCenter + 2 + i);
        data.indices.push_back(bottomCenter + 1 + i);
    }

    data.calculateBounds();
    data.calculateTangents();

    return uploadMesh(data, "Cone");
}

Mesh MeshUtils::createTorus(float outerRadius, float innerRadius, uint32_t segments, uint32_t rings) {
    MeshData data;

    for (uint32_t ring = 0; ring <= rings; ++ring) {
        float phi = TWO_PI * float(ring) / float(rings);
        float cosPhi = std::cos(phi);
        float sinPhi = std::sin(phi);

        for (uint32_t seg = 0; seg <= segments; ++seg) {
            float theta = TWO_PI * float(seg) / float(segments);
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);

            float x = (outerRadius + innerRadius * cosTheta) * cosPhi;
            float y = innerRadius * sinTheta;
            float z = (outerRadius + innerRadius * cosTheta) * sinPhi;

            glm::vec3 center(outerRadius * cosPhi, 0.0f, outerRadius * sinPhi);
            glm::vec3 position(x, y, z);
            glm::vec3 normal = glm::normalize(position - center);

            glm::vec2 uv(float(seg) / float(segments), float(ring) / float(rings));

            data.vertices.push_back({position, normal, uv});
        }
    }

    // Indices
    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t seg = 0; seg < segments; ++seg) {
            uint32_t current = ring * (segments + 1) + seg;
            uint32_t next = current + segments + 1;

            data.indices.push_back(current);
            data.indices.push_back(next);
            data.indices.push_back(current + 1);

            data.indices.push_back(current + 1);
            data.indices.push_back(next);
            data.indices.push_back(next + 1);
        }
    }

    data.calculateBounds();
    data.calculateTangents();

    return uploadMesh(data, "Torus");
}

} // namespace vivid
