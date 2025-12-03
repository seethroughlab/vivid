#include "mesh.h"
#include "renderer.h"
#include <cmath>
#include <iostream>

namespace vivid {

// Vertex attribute descriptions for pipeline creation
static WGPUVertexAttribute vertexAttributes[4] = {};
static WGPUVertexBufferLayout vertexLayout = {};
static bool layoutInitialized = false;

WGPUVertexBufferLayout Mesh::getVertexLayout() {
    if (!layoutInitialized) {
        // Position: vec3f @ location(0)
        vertexAttributes[0].format = WGPUVertexFormat_Float32x3;
        vertexAttributes[0].offset = offsetof(Vertex3D, position);
        vertexAttributes[0].shaderLocation = 0;

        // Normal: vec3f @ location(1)
        vertexAttributes[1].format = WGPUVertexFormat_Float32x3;
        vertexAttributes[1].offset = offsetof(Vertex3D, normal);
        vertexAttributes[1].shaderLocation = 1;

        // UV: vec2f @ location(2)
        vertexAttributes[2].format = WGPUVertexFormat_Float32x2;
        vertexAttributes[2].offset = offsetof(Vertex3D, uv);
        vertexAttributes[2].shaderLocation = 2;

        // Tangent: vec4f @ location(3)
        vertexAttributes[3].format = WGPUVertexFormat_Float32x4;
        vertexAttributes[3].offset = offsetof(Vertex3D, tangent);
        vertexAttributes[3].shaderLocation = 3;

        vertexLayout.arrayStride = sizeof(Vertex3D);
        vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexLayout.attributeCount = 4;
        vertexLayout.attributes = vertexAttributes;

        layoutInitialized = true;
    }
    return vertexLayout;
}

Mesh::~Mesh() {
    destroy();
}

Mesh::Mesh(Mesh&& other) noexcept
    : vertexBuffer_(other.vertexBuffer_)
    , indexBuffer_(other.indexBuffer_)
    , vertexCount_(other.vertexCount_)
    , indexCount_(other.indexCount_)
    , bounds_(other.bounds_)
#ifdef VIVID_USE_DILIGENT
    , diligentMesh_(other.diligentMesh_)
#endif
{
    other.vertexBuffer_ = nullptr;
    other.indexBuffer_ = nullptr;
    other.vertexCount_ = 0;
    other.indexCount_ = 0;
#ifdef VIVID_USE_DILIGENT
    other.diligentMesh_ = DiligentMeshData{};
#endif
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        destroy();
        vertexBuffer_ = other.vertexBuffer_;
        indexBuffer_ = other.indexBuffer_;
        vertexCount_ = other.vertexCount_;
        indexCount_ = other.indexCount_;
        bounds_ = other.bounds_;
#ifdef VIVID_USE_DILIGENT
        diligentMesh_ = other.diligentMesh_;
#endif
        other.vertexBuffer_ = nullptr;
        other.indexBuffer_ = nullptr;
        other.vertexCount_ = 0;
        other.indexCount_ = 0;
#ifdef VIVID_USE_DILIGENT
        other.diligentMesh_ = DiligentMeshData{};
#endif
    }
    return *this;
}

bool Mesh::create(Renderer& renderer,
                  const std::vector<Vertex3D>& vertices,
                  const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) {
        std::cerr << "[Mesh] Cannot create empty mesh\n";
        return false;
    }

    if (indices.size() % 3 != 0) {
        std::cerr << "[Mesh] Index count must be multiple of 3\n";
        return false;
    }

    destroy();

    WGPUDevice device = renderer.device();
    WGPUQueue queue = renderer.queue();

    // Create vertex buffer
    size_t vertexDataSize = vertices.size() * sizeof(Vertex3D);
    WGPUBufferDescriptor vbDesc = {};
    vbDesc.size = vertexDataSize;
    vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;

    vertexBuffer_ = wgpuDeviceCreateBuffer(device, &vbDesc);
    if (!vertexBuffer_) {
        std::cerr << "[Mesh] Failed to create vertex buffer\n";
        return false;
    }

    wgpuQueueWriteBuffer(queue, vertexBuffer_, 0, vertices.data(), vertexDataSize);

    // Create index buffer
    size_t indexDataSize = indices.size() * sizeof(uint32_t);
    WGPUBufferDescriptor ibDesc = {};
    ibDesc.size = indexDataSize;
    ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;

    indexBuffer_ = wgpuDeviceCreateBuffer(device, &ibDesc);
    if (!indexBuffer_) {
        std::cerr << "[Mesh] Failed to create index buffer\n";
        wgpuBufferRelease(vertexBuffer_);
        vertexBuffer_ = nullptr;
        return false;
    }

    wgpuQueueWriteBuffer(queue, indexBuffer_, 0, indices.data(), indexDataSize);

    vertexCount_ = static_cast<uint32_t>(vertices.size());
    indexCount_ = static_cast<uint32_t>(indices.size());

    // Calculate bounding box
    bounds_ = BoundingBox{};
    for (const auto& v : vertices) {
        bounds_.expand(v.position);
    }

    return true;
}

void Mesh::destroy() {
    if (indexBuffer_) {
        wgpuBufferRelease(indexBuffer_);
        indexBuffer_ = nullptr;
    }
    if (vertexBuffer_) {
        wgpuBufferRelease(vertexBuffer_);
        vertexBuffer_ = nullptr;
    }
    vertexCount_ = 0;
    indexCount_ = 0;
    bounds_ = BoundingBox{};
#ifdef VIVID_USE_DILIGENT
    // Note: DiligentMeshData buffers are owned by DiligentPBR
    diligentMesh_ = DiligentMeshData{};
#endif
}

#ifdef VIVID_USE_DILIGENT
bool Mesh::createDiligentMesh(DiligentPBR& pbr,
                               const std::vector<Vertex3D>& vertices,
                               const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) {
        return false;
    }
    diligentMesh_ = pbr.createMesh(vertices, indices);
    return diligentMesh_.vertexBuffer != nullptr;
}
#endif

void Mesh::draw(WGPURenderPassEncoder encoder, uint32_t instanceCount) const {
    if (!valid()) return;

    wgpuRenderPassEncoderSetVertexBuffer(encoder, 0, vertexBuffer_, 0,
                                          vertexCount_ * sizeof(Vertex3D));
    wgpuRenderPassEncoderSetIndexBuffer(encoder, indexBuffer_, WGPUIndexFormat_Uint32,
                                         0, indexCount_ * sizeof(uint32_t));
    wgpuRenderPassEncoderDrawIndexed(encoder, indexCount_, instanceCount, 0, 0, 0);
}

// Primitive generators
namespace primitives {

void generateCube(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    // 6 faces, 4 vertices each = 24 vertices
    // Each face has its own normals for flat shading
    const float s = 0.5f; // half-size

    // Front face (+Z)
    vertices.push_back({{-s, -s,  s}, { 0,  0,  1}, {0, 1}, {1, 0, 0, 1}});
    vertices.push_back({{ s, -s,  s}, { 0,  0,  1}, {1, 1}, {1, 0, 0, 1}});
    vertices.push_back({{ s,  s,  s}, { 0,  0,  1}, {1, 0}, {1, 0, 0, 1}});
    vertices.push_back({{-s,  s,  s}, { 0,  0,  1}, {0, 0}, {1, 0, 0, 1}});

    // Back face (-Z)
    vertices.push_back({{ s, -s, -s}, { 0,  0, -1}, {0, 1}, {-1, 0, 0, 1}});
    vertices.push_back({{-s, -s, -s}, { 0,  0, -1}, {1, 1}, {-1, 0, 0, 1}});
    vertices.push_back({{-s,  s, -s}, { 0,  0, -1}, {1, 0}, {-1, 0, 0, 1}});
    vertices.push_back({{ s,  s, -s}, { 0,  0, -1}, {0, 0}, {-1, 0, 0, 1}});

    // Top face (+Y)
    vertices.push_back({{-s,  s,  s}, { 0,  1,  0}, {0, 1}, {1, 0, 0, 1}});
    vertices.push_back({{ s,  s,  s}, { 0,  1,  0}, {1, 1}, {1, 0, 0, 1}});
    vertices.push_back({{ s,  s, -s}, { 0,  1,  0}, {1, 0}, {1, 0, 0, 1}});
    vertices.push_back({{-s,  s, -s}, { 0,  1,  0}, {0, 0}, {1, 0, 0, 1}});

    // Bottom face (-Y)
    vertices.push_back({{-s, -s, -s}, { 0, -1,  0}, {0, 1}, {1, 0, 0, 1}});
    vertices.push_back({{ s, -s, -s}, { 0, -1,  0}, {1, 1}, {1, 0, 0, 1}});
    vertices.push_back({{ s, -s,  s}, { 0, -1,  0}, {1, 0}, {1, 0, 0, 1}});
    vertices.push_back({{-s, -s,  s}, { 0, -1,  0}, {0, 0}, {1, 0, 0, 1}});

    // Right face (+X)
    vertices.push_back({{ s, -s,  s}, { 1,  0,  0}, {0, 1}, {0, 0, 1, 1}});
    vertices.push_back({{ s, -s, -s}, { 1,  0,  0}, {1, 1}, {0, 0, 1, 1}});
    vertices.push_back({{ s,  s, -s}, { 1,  0,  0}, {1, 0}, {0, 0, 1, 1}});
    vertices.push_back({{ s,  s,  s}, { 1,  0,  0}, {0, 0}, {0, 0, 1, 1}});

    // Left face (-X)
    vertices.push_back({{-s, -s, -s}, {-1,  0,  0}, {0, 1}, {0, 0, -1, 1}});
    vertices.push_back({{-s, -s,  s}, {-1,  0,  0}, {1, 1}, {0, 0, -1, 1}});
    vertices.push_back({{-s,  s,  s}, {-1,  0,  0}, {1, 0}, {0, 0, -1, 1}});
    vertices.push_back({{-s,  s, -s}, {-1,  0,  0}, {0, 0}, {0, 0, -1, 1}});

    // Indices for 6 faces (2 triangles each)
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
}

void generatePlane(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                   float width, float height, int subdivisionsX, int subdivisionsZ) {
    vertices.clear();
    indices.clear();

    subdivisionsX = std::max(1, subdivisionsX);
    subdivisionsZ = std::max(1, subdivisionsZ);

    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;

    // Generate vertices
    for (int z = 0; z <= subdivisionsZ; ++z) {
        for (int x = 0; x <= subdivisionsX; ++x) {
            float u = static_cast<float>(x) / subdivisionsX;
            float v = static_cast<float>(z) / subdivisionsZ;

            Vertex3D vert;
            vert.position = {
                -halfWidth + u * width,
                0.0f,
                -halfHeight + v * height
            };
            vert.normal = {0.0f, 1.0f, 0.0f};
            vert.uv = {u, v};
            vert.tangent = {1.0f, 0.0f, 0.0f, 1.0f};

            vertices.push_back(vert);
        }
    }

    // Generate indices
    int vertsPerRow = subdivisionsX + 1;
    for (int z = 0; z < subdivisionsZ; ++z) {
        for (int x = 0; x < subdivisionsX; ++x) {
            uint32_t topLeft = z * vertsPerRow + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = topLeft + vertsPerRow;
            uint32_t bottomRight = bottomLeft + 1;

            // Two triangles per quad
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }
}

void generateSphere(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                    float radius, int segments, int rings) {
    vertices.clear();
    indices.clear();

    segments = std::max(3, segments);
    rings = std::max(2, rings);

    const float PI = 3.14159265359f;

    // Generate vertices
    for (int ring = 0; ring <= rings; ++ring) {
        float phi = PI * static_cast<float>(ring) / rings;
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);

        for (int seg = 0; seg <= segments; ++seg) {
            float theta = 2.0f * PI * static_cast<float>(seg) / segments;
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            Vertex3D vert;
            vert.normal = {
                sinPhi * cosTheta,
                cosPhi,
                sinPhi * sinTheta
            };
            vert.position = vert.normal * radius;
            vert.uv = {
                static_cast<float>(seg) / segments,
                static_cast<float>(ring) / rings
            };
            // Tangent: perpendicular to normal in XZ plane
            vert.tangent = {-sinTheta, 0.0f, cosTheta, 1.0f};

            vertices.push_back(vert);
        }
    }

    // Generate indices
    int vertsPerRow = segments + 1;
    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            uint32_t current = ring * vertsPerRow + seg;
            uint32_t next = current + vertsPerRow;

            // Two triangles per quad
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);

            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
}

void generateCylinder(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                      float radius, float height, int segments) {
    vertices.clear();
    indices.clear();

    segments = std::max(3, segments);

    const float PI = 3.14159265359f;
    float halfHeight = height * 0.5f;

    // Side vertices
    for (int i = 0; i <= segments; ++i) {
        float theta = 2.0f * PI * static_cast<float>(i) / segments;
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        // Bottom vertex
        Vertex3D bottom;
        bottom.position = {radius * cosT, -halfHeight, radius * sinT};
        bottom.normal = {cosT, 0.0f, sinT};
        bottom.uv = {static_cast<float>(i) / segments, 1.0f};
        bottom.tangent = {-sinT, 0.0f, cosT, 1.0f};
        vertices.push_back(bottom);

        // Top vertex
        Vertex3D top;
        top.position = {radius * cosT, halfHeight, radius * sinT};
        top.normal = {cosT, 0.0f, sinT};
        top.uv = {static_cast<float>(i) / segments, 0.0f};
        top.tangent = {-sinT, 0.0f, cosT, 1.0f};
        vertices.push_back(top);
    }

    // Side indices
    for (int i = 0; i < segments; ++i) {
        uint32_t bl = i * 2;
        uint32_t tl = bl + 1;
        uint32_t br = bl + 2;
        uint32_t tr = bl + 3;

        indices.push_back(bl);
        indices.push_back(br);
        indices.push_back(tl);

        indices.push_back(tl);
        indices.push_back(br);
        indices.push_back(tr);
    }

    // Top cap center
    uint32_t topCenter = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{0.0f, halfHeight, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}});

    // Top cap ring
    uint32_t topRingStart = static_cast<uint32_t>(vertices.size());
    for (int i = 0; i <= segments; ++i) {
        float theta = 2.0f * PI * static_cast<float>(i) / segments;
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        Vertex3D vert;
        vert.position = {radius * cosT, halfHeight, radius * sinT};
        vert.normal = {0.0f, 1.0f, 0.0f};
        vert.uv = {cosT * 0.5f + 0.5f, sinT * 0.5f + 0.5f};
        vert.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        vertices.push_back(vert);
    }

    // Top cap indices
    for (int i = 0; i < segments; ++i) {
        indices.push_back(topCenter);
        indices.push_back(topRingStart + i + 1);
        indices.push_back(topRingStart + i);
    }

    // Bottom cap center
    uint32_t bottomCenter = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{0.0f, -halfHeight, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}});

    // Bottom cap ring
    uint32_t bottomRingStart = static_cast<uint32_t>(vertices.size());
    for (int i = 0; i <= segments; ++i) {
        float theta = 2.0f * PI * static_cast<float>(i) / segments;
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        Vertex3D vert;
        vert.position = {radius * cosT, -halfHeight, radius * sinT};
        vert.normal = {0.0f, -1.0f, 0.0f};
        vert.uv = {cosT * 0.5f + 0.5f, sinT * 0.5f + 0.5f};
        vert.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        vertices.push_back(vert);
    }

    // Bottom cap indices (reversed winding)
    for (int i = 0; i < segments; ++i) {
        indices.push_back(bottomCenter);
        indices.push_back(bottomRingStart + i);
        indices.push_back(bottomRingStart + i + 1);
    }
}

void generateTorus(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                   float majorRadius, float minorRadius,
                   int majorSegments, int minorSegments) {
    vertices.clear();
    indices.clear();

    majorSegments = std::max(3, majorSegments);
    minorSegments = std::max(3, minorSegments);

    const float PI = 3.14159265359f;

    for (int i = 0; i <= majorSegments; ++i) {
        float u = static_cast<float>(i) / majorSegments;
        float theta = u * 2.0f * PI;
        float cosTheta = std::cos(theta);
        float sinTheta = std::sin(theta);

        // Center of the tube at this major segment
        glm::vec3 center = {majorRadius * cosTheta, 0.0f, majorRadius * sinTheta};

        for (int j = 0; j <= minorSegments; ++j) {
            float v = static_cast<float>(j) / minorSegments;
            float phi = v * 2.0f * PI;
            float cosPhi = std::cos(phi);
            float sinPhi = std::sin(phi);

            Vertex3D vert;

            // Normal pointing outward from tube center
            glm::vec3 normal = {
                cosPhi * cosTheta,
                sinPhi,
                cosPhi * sinTheta
            };
            vert.normal = normal;

            // Position: center + normal * minorRadius
            vert.position = center + normal * minorRadius;

            vert.uv = {u, v};

            // Tangent: direction along the major circle
            vert.tangent = {-sinTheta, 0.0f, cosTheta, 1.0f};

            vertices.push_back(vert);
        }
    }

    // Generate indices
    int vertsPerRow = minorSegments + 1;
    for (int i = 0; i < majorSegments; ++i) {
        for (int j = 0; j < minorSegments; ++j) {
            uint32_t current = i * vertsPerRow + j;
            uint32_t next = current + vertsPerRow;

            // CCW winding for front faces (outward normals)
            indices.push_back(current);
            indices.push_back(current + 1);
            indices.push_back(next);

            indices.push_back(current + 1);
            indices.push_back(next + 1);
            indices.push_back(next);
        }
    }
}

void generateEllipticTorus(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                           float majorRadiusX, float majorRadiusZ, float minorRadius,
                           int majorSegments, int minorSegments) {
    vertices.clear();
    indices.clear();

    majorSegments = std::max(3, majorSegments);
    minorSegments = std::max(3, minorSegments);

    const float PI = 3.14159265359f;

    for (int i = 0; i <= majorSegments; ++i) {
        float u = static_cast<float>(i) / majorSegments;
        float theta = u * 2.0f * PI;
        float cosTheta = std::cos(theta);
        float sinTheta = std::sin(theta);

        // Center of the tube at this major segment (elliptical path)
        glm::vec3 center = {majorRadiusX * cosTheta, 0.0f, majorRadiusZ * sinTheta};

        // Calculate tangent along ellipse for proper orientation
        // Derivative of (a*cos(t), 0, b*sin(t)) = (-a*sin(t), 0, b*cos(t))
        glm::vec3 tangent = glm::normalize(glm::vec3(-majorRadiusX * sinTheta, 0.0f, majorRadiusZ * cosTheta));

        // Normal to the ellipse path (outward from center in XZ plane)
        glm::vec3 pathNormal = glm::normalize(glm::vec3(cosTheta / majorRadiusX, 0.0f, sinTheta / majorRadiusZ));
        pathNormal = glm::normalize(pathNormal);  // Re-normalize for numerical stability

        for (int j = 0; j <= minorSegments; ++j) {
            float v = static_cast<float>(j) / minorSegments;
            float phi = v * 2.0f * PI;
            float cosPhi = std::cos(phi);
            float sinPhi = std::sin(phi);

            Vertex3D vert;

            // Normal pointing outward from tube center
            // Combine path normal (outward in XZ) with up vector for tube cross-section
            glm::vec3 normal = pathNormal * cosPhi + glm::vec3(0.0f, 1.0f, 0.0f) * sinPhi;
            vert.normal = glm::normalize(normal);

            // Position: center + normal * minorRadius
            vert.position = center + vert.normal * minorRadius;

            vert.uv = {u, v};

            // Tangent: direction along the major ellipse
            vert.tangent = glm::vec4(tangent, 1.0f);

            vertices.push_back(vert);
        }
    }

    // Generate indices
    int vertsPerRow = minorSegments + 1;
    for (int i = 0; i < majorSegments; ++i) {
        for (int j = 0; j < minorSegments; ++j) {
            uint32_t current = i * vertsPerRow + j;
            uint32_t next = current + vertsPerRow;

            // CCW winding for front faces (outward normals)
            indices.push_back(current);
            indices.push_back(current + 1);
            indices.push_back(next);

            indices.push_back(current + 1);
            indices.push_back(next + 1);
            indices.push_back(next);
        }
    }
}

void generateCone(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                  float radius, float height, int segments) {
    vertices.clear();
    indices.clear();

    segments = std::max(3, segments);

    const float PI = 3.14159265359f;

    // Apex at origin, base at -height (pointing down -Y by default)
    glm::vec3 apex = {0.0f, 0.0f, 0.0f};
    float baseY = -height;

    // Side vertices - for each segment, we need apex and base edge vertex
    // Using separate vertices per face for flat shading
    for (int i = 0; i < segments; ++i) {
        float theta0 = 2.0f * PI * static_cast<float>(i) / segments;
        float theta1 = 2.0f * PI * static_cast<float>(i + 1) / segments;

        float cos0 = std::cos(theta0);
        float sin0 = std::sin(theta0);
        float cos1 = std::cos(theta1);
        float sin1 = std::sin(theta1);

        glm::vec3 base0 = {radius * cos0, baseY, radius * sin0};
        glm::vec3 base1 = {radius * cos1, baseY, radius * sin1};

        // Calculate face normal
        glm::vec3 edge1 = base0 - apex;
        glm::vec3 edge2 = base1 - apex;
        glm::vec3 normal = glm::normalize(glm::cross(edge2, edge1));

        uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

        // Triangle: apex, base0, base1
        vertices.push_back({apex, normal, {0.5f, 0.0f}, {1, 0, 0, 1}});
        vertices.push_back({base0, normal, {static_cast<float>(i) / segments, 1.0f}, {1, 0, 0, 1}});
        vertices.push_back({base1, normal, {static_cast<float>(i + 1) / segments, 1.0f}, {1, 0, 0, 1}});

        indices.push_back(baseIdx);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 2);
    }

    // Base cap center
    uint32_t baseCenter = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{0.0f, baseY, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f}, {1, 0, 0, 1}});

    // Base cap ring
    uint32_t baseRingStart = static_cast<uint32_t>(vertices.size());
    for (int i = 0; i <= segments; ++i) {
        float theta = 2.0f * PI * static_cast<float>(i) / segments;
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        Vertex3D vert;
        vert.position = {radius * cosT, baseY, radius * sinT};
        vert.normal = {0.0f, -1.0f, 0.0f};
        vert.uv = {cosT * 0.5f + 0.5f, sinT * 0.5f + 0.5f};
        vert.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        vertices.push_back(vert);
    }

    // Base cap indices (reversed winding for downward normal)
    for (int i = 0; i < segments; ++i) {
        indices.push_back(baseCenter);
        indices.push_back(baseRingStart + i);
        indices.push_back(baseRingStart + i + 1);
    }
}

void generateDirectionalLightGizmo(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                                    float lineLength, float lineSpacing, int lineCount) {
    vertices.clear();
    indices.clear();

    // Generate a grid of parallel lines pointing in -Y direction
    // Lines are thin quads for visibility
    const float lineWidth = 0.02f;

    int gridSize = static_cast<int>(std::sqrt(static_cast<float>(lineCount)));
    if (gridSize < 1) gridSize = 1;

    float halfExtent = (gridSize - 1) * lineSpacing * 0.5f;

    for (int x = 0; x < gridSize; ++x) {
        for (int z = 0; z < gridSize; ++z) {
            float px = -halfExtent + x * lineSpacing;
            float pz = -halfExtent + z * lineSpacing;

            // Line starts at (px, 0, pz) and goes to (px, -lineLength, pz)
            // Create as thin quad facing outward
            uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

            // Quad in XY plane (line thickness in X)
            glm::vec3 p0 = {px - lineWidth, 0.0f, pz};
            glm::vec3 p1 = {px + lineWidth, 0.0f, pz};
            glm::vec3 p2 = {px + lineWidth, -lineLength, pz};
            glm::vec3 p3 = {px - lineWidth, -lineLength, pz};
            glm::vec3 normal = {0.0f, 0.0f, 1.0f};

            vertices.push_back({p0, normal, {0, 0}, {1, 0, 0, 1}});
            vertices.push_back({p1, normal, {1, 0}, {1, 0, 0, 1}});
            vertices.push_back({p2, normal, {1, 1}, {1, 0, 0, 1}});
            vertices.push_back({p3, normal, {0, 1}, {1, 0, 0, 1}});

            indices.push_back(baseIdx);
            indices.push_back(baseIdx + 1);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 3);

            // Add arrowhead at end of each line
            float arrowSize = lineWidth * 3.0f;
            uint32_t arrowIdx = static_cast<uint32_t>(vertices.size());

            // Triangle pointing down
            glm::vec3 a0 = {px, -lineLength, pz};  // tip
            glm::vec3 a1 = {px - arrowSize, -lineLength + arrowSize * 2, pz};
            glm::vec3 a2 = {px + arrowSize, -lineLength + arrowSize * 2, pz};

            vertices.push_back({a0, normal, {0.5f, 1}, {1, 0, 0, 1}});
            vertices.push_back({a1, normal, {0, 0}, {1, 0, 0, 1}});
            vertices.push_back({a2, normal, {1, 0}, {1, 0, 0, 1}});

            indices.push_back(arrowIdx);
            indices.push_back(arrowIdx + 2);
            indices.push_back(arrowIdx + 1);
        }
    }
}

void generateSpotLightGizmo(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                            float length, float angle, int segments) {
    vertices.clear();
    indices.clear();

    segments = std::max(4, segments);

    const float PI = 3.14159265359f;

    // Cone apex at origin, opening toward -Y
    // angle is half-angle in radians
    float radius = length * std::tan(angle);

    // Generate wireframe cone - just the edges, not filled faces
    // For wireframe rendering, we still need triangles, but thin ones

    const float lineWidth = 0.015f;

    // Lines from apex to base circle
    for (int i = 0; i < segments; ++i) {
        float theta = 2.0f * PI * static_cast<float>(i) / segments;
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        glm::vec3 basePoint = {radius * cosT, -length, radius * sinT};
        glm::vec3 apex = {0.0f, 0.0f, 0.0f};

        // Create thin quad from apex to base point
        glm::vec3 dir = glm::normalize(basePoint - apex);
        glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0, 0, 1)));
        if (glm::length(right) < 0.001f) {
            right = glm::normalize(glm::cross(dir, glm::vec3(1, 0, 0)));
        }

        uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

        glm::vec3 p0 = apex - right * lineWidth;
        glm::vec3 p1 = apex + right * lineWidth;
        glm::vec3 p2 = basePoint + right * lineWidth;
        glm::vec3 p3 = basePoint - right * lineWidth;
        glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p3 - p0));

        vertices.push_back({p0, normal, {0, 0}, {1, 0, 0, 1}});
        vertices.push_back({p1, normal, {1, 0}, {1, 0, 0, 1}});
        vertices.push_back({p2, normal, {1, 1}, {1, 0, 0, 1}});
        vertices.push_back({p3, normal, {0, 1}, {1, 0, 0, 1}});

        indices.push_back(baseIdx);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 3);
    }

    // Base circle
    for (int i = 0; i < segments; ++i) {
        float theta0 = 2.0f * PI * static_cast<float>(i) / segments;
        float theta1 = 2.0f * PI * static_cast<float>(i + 1) / segments;

        float cos0 = std::cos(theta0);
        float sin0 = std::sin(theta0);
        float cos1 = std::cos(theta1);
        float sin1 = std::sin(theta1);

        glm::vec3 p0 = {radius * cos0, -length, radius * sin0};
        glm::vec3 p1 = {radius * cos1, -length, radius * sin1};

        // Create thin quad for circle segment
        glm::vec3 up = {0.0f, 1.0f, 0.0f};
        glm::vec3 dir = glm::normalize(p1 - p0);
        glm::vec3 out = glm::normalize(glm::cross(dir, up));

        uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

        glm::vec3 q0 = p0 - out * lineWidth;
        glm::vec3 q1 = p0 + out * lineWidth;
        glm::vec3 q2 = p1 + out * lineWidth;
        glm::vec3 q3 = p1 - out * lineWidth;

        vertices.push_back({q0, up, {0, 0}, {1, 0, 0, 1}});
        vertices.push_back({q1, up, {1, 0}, {1, 0, 0, 1}});
        vertices.push_back({q2, up, {1, 1}, {1, 0, 0, 1}});
        vertices.push_back({q3, up, {0, 1}, {1, 0, 0, 1}});

        indices.push_back(baseIdx);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 3);
    }
}

void generatePointLightGizmo(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                              float radius, int segments) {
    // Generate 3 wireframe circles (XY, XZ, YZ planes) for point light
    vertices.clear();
    indices.clear();

    segments = std::max(8, segments);

    const float PI = 3.14159265359f;
    const float lineWidth = 0.015f;

    auto addCircle = [&](int axis1, int axis2, int normalAxis) {
        for (int i = 0; i < segments; ++i) {
            float theta0 = 2.0f * PI * static_cast<float>(i) / segments;
            float theta1 = 2.0f * PI * static_cast<float>(i + 1) / segments;

            glm::vec3 p0 = glm::vec3(0.0f);
            glm::vec3 p1 = glm::vec3(0.0f);

            p0[axis1] = radius * std::cos(theta0);
            p0[axis2] = radius * std::sin(theta0);
            p1[axis1] = radius * std::cos(theta1);
            p1[axis2] = radius * std::sin(theta1);

            // Normal perpendicular to circle plane
            glm::vec3 normal = glm::vec3(0.0f);
            normal[normalAxis] = 1.0f;

            // Direction along circle
            glm::vec3 dir = glm::normalize(p1 - p0);
            glm::vec3 out = glm::normalize(glm::cross(dir, normal));

            uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

            glm::vec3 q0 = p0 - out * lineWidth;
            glm::vec3 q1 = p0 + out * lineWidth;
            glm::vec3 q2 = p1 + out * lineWidth;
            glm::vec3 q3 = p1 - out * lineWidth;

            vertices.push_back({q0, normal, {0, 0}, {1, 0, 0, 1}});
            vertices.push_back({q1, normal, {1, 0}, {1, 0, 0, 1}});
            vertices.push_back({q2, normal, {1, 1}, {1, 0, 0, 1}});
            vertices.push_back({q3, normal, {0, 1}, {1, 0, 0, 1}});

            indices.push_back(baseIdx);
            indices.push_back(baseIdx + 1);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 3);
        }
    };

    // XY plane circle (around Z axis)
    addCircle(0, 1, 2);
    // XZ plane circle (around Y axis)
    addCircle(0, 2, 1);
    // YZ plane circle (around X axis)
    addCircle(1, 2, 0);
}

} // namespace primitives
} // namespace vivid
