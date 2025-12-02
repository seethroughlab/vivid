#include <vivid/csg/csg.h>
#include <manifold/manifold.h>
#include <cmath>

namespace vivid::csg {

// === CSGMesh Implementation ===

CSGMesh& CSGMesh::append(const CSGMesh& other) {
    uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
    for (uint32_t idx : other.indices) {
        indices.push_back(baseIndex + idx);
    }
    return *this;
}

// === Solid Implementation ===

struct Solid::Impl {
    manifold::Manifold manifold;

    Impl() = default;
    Impl(manifold::Manifold m) : manifold(std::move(m)) {}
};

Solid::Solid() : impl_(std::make_unique<Impl>()) {}
Solid::~Solid() = default;

Solid::Solid(const Solid& other)
    : impl_(std::make_unique<Impl>(other.impl_->manifold)) {}

Solid& Solid::operator=(const Solid& other) {
    if (this != &other) {
        impl_ = std::make_unique<Impl>(other.impl_->manifold);
    }
    return *this;
}

Solid::Solid(Solid&& other) noexcept = default;
Solid& Solid::operator=(Solid&& other) noexcept = default;

// Helper to convert glm::vec3 to manifold::vec3
static manifold::vec3 toMVec3(float x, float y, float z) {
    return manifold::vec3(static_cast<double>(x),
                          static_cast<double>(y),
                          static_cast<double>(z));
}

static manifold::vec3 toMVec3(glm::vec3 v) {
    return toMVec3(v.x, v.y, v.z);
}

// Helper to create Solid from a MeshGL - converts through vertices/indices
static Solid solidFromMeshGL(const manifold::MeshGL& mesh) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    int numProp = mesh.numProp;
    vertices.reserve(mesh.NumVert());

    for (size_t i = 0; i < mesh.NumVert(); ++i) {
        Vertex3D v;
        v.position = glm::vec3(
            mesh.vertProperties[i * numProp + 0],
            mesh.vertProperties[i * numProp + 1],
            mesh.vertProperties[i * numProp + 2]
        );
        v.normal = glm::vec3(0, 1, 0);  // Will be recalculated
        v.uv = glm::vec2(0);
        vertices.push_back(v);
    }

    indices = mesh.triVerts;

    return Solid::fromMesh(vertices, indices);
}

// === PRIMITIVES ===

Solid Solid::box(float width, float height, float depth) {
    Solid s;
    s.impl_->manifold = manifold::Manifold::Cube(
        toMVec3(width, height, depth), true);  // true = center at origin
    return s;
}

Solid Solid::box(glm::vec3 size) {
    return box(size.x, size.y, size.z);
}

Solid Solid::sphere(float radius, int segments) {
    Solid s;
    s.impl_->manifold = manifold::Manifold::Sphere(static_cast<double>(radius), segments);
    return s;
}

Solid Solid::cylinder(float radius, float height, int segments) {
    Solid s;
    // Manifold's cylinder is centered at origin along Z axis
    // We want it along Y axis, so we'll rotate it
    s.impl_->manifold = manifold::Manifold::Cylinder(
        static_cast<double>(height),
        static_cast<double>(radius),
        static_cast<double>(radius),
        segments, true)
        .Rotate(90.0, 0.0, 0.0);  // Rotate to Y-up (uses degrees)
    return s;
}

Solid Solid::cone(float radius, float height, int segments) {
    Solid s;
    // Cone with 0 radius at top
    s.impl_->manifold = manifold::Manifold::Cylinder(
        static_cast<double>(height),
        static_cast<double>(radius),
        0.0,
        segments, false)
        .Rotate(90.0, 0.0, 0.0);  // Rotate to Y-up
    return s;
}

Solid Solid::torus(float majorRadius, float minorRadius,
                   int majorSegments, int minorSegments) {
    // Build torus mesh manually
    std::vector<float> vertProperties;
    std::vector<uint32_t> triVerts;

    const double PI = 3.14159265358979323846;

    // Generate vertices
    for (int i = 0; i < majorSegments; ++i) {
        double majorAngle = 2.0 * PI * i / majorSegments;
        double cosM = std::cos(majorAngle);
        double sinM = std::sin(majorAngle);

        for (int j = 0; j < minorSegments; ++j) {
            double minorAngle = 2.0 * PI * j / minorSegments;
            double r = majorRadius + minorRadius * std::cos(minorAngle);
            double y = minorRadius * std::sin(minorAngle);

            // Position (x, y, z)
            vertProperties.push_back(static_cast<float>(r * cosM));
            vertProperties.push_back(static_cast<float>(y));
            vertProperties.push_back(static_cast<float>(r * sinM));
        }
    }

    // Create triangles
    for (int i = 0; i < majorSegments; ++i) {
        int nextI = (i + 1) % majorSegments;
        for (int j = 0; j < minorSegments; ++j) {
            int nextJ = (j + 1) % minorSegments;

            uint32_t v00 = i * minorSegments + j;
            uint32_t v10 = nextI * minorSegments + j;
            uint32_t v01 = i * minorSegments + nextJ;
            uint32_t v11 = nextI * minorSegments + nextJ;

            triVerts.push_back(v00);
            triVerts.push_back(v10);
            triVerts.push_back(v11);

            triVerts.push_back(v00);
            triVerts.push_back(v11);
            triVerts.push_back(v01);
        }
    }

    manifold::MeshGL mesh;
    mesh.numProp = 3;
    mesh.vertProperties = vertProperties;
    mesh.triVerts = triVerts;

    Solid s;
    s.impl_->manifold = manifold::Manifold(mesh);
    return s;
}

Solid Solid::fromMesh(const std::vector<Vertex3D>& vertices,
                      const std::vector<uint32_t>& indices) {
    manifold::MeshGL mesh;
    mesh.numProp = 3;  // Just positions

    // Convert vertices (only positions for CSG)
    mesh.vertProperties.reserve(vertices.size() * 3);
    for (const auto& v : vertices) {
        mesh.vertProperties.push_back(v.position.x);
        mesh.vertProperties.push_back(v.position.y);
        mesh.vertProperties.push_back(v.position.z);
    }

    // Copy indices directly
    mesh.triVerts = indices;

    Solid s;
    s.impl_->manifold = manifold::Manifold(mesh);
    return s;
}

// === BOOLEAN OPERATIONS ===

Solid Solid::unite(const Solid& other) const {
    Solid result;
    result.impl_->manifold = impl_->manifold + other.impl_->manifold;
    return result;
}

Solid Solid::subtract(const Solid& other) const {
    Solid result;
    result.impl_->manifold = impl_->manifold - other.impl_->manifold;
    return result;
}

Solid Solid::intersect(const Solid& other) const {
    Solid result;
    result.impl_->manifold = impl_->manifold ^ other.impl_->manifold;
    return result;
}

// === TRANSFORMS ===

Solid Solid::translate(float x, float y, float z) const {
    Solid result;
    result.impl_->manifold = impl_->manifold.Translate(toMVec3(x, y, z));
    return result;
}

Solid Solid::translate(glm::vec3 offset) const {
    return translate(offset.x, offset.y, offset.z);
}

Solid Solid::rotate(float angle, glm::vec3 axis) const {
    // For arbitrary axis rotation, we use the transform matrix
    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), angle, glm::normalize(axis));
    return transform(rot);
}

Solid Solid::rotateX(float angle) const {
    double degrees = angle * 180.0 / 3.14159265358979323846;
    Solid result;
    result.impl_->manifold = impl_->manifold.Rotate(degrees, 0.0, 0.0);
    return result;
}

Solid Solid::rotateY(float angle) const {
    double degrees = angle * 180.0 / 3.14159265358979323846;
    Solid result;
    result.impl_->manifold = impl_->manifold.Rotate(0.0, degrees, 0.0);
    return result;
}

Solid Solid::rotateZ(float angle) const {
    double degrees = angle * 180.0 / 3.14159265358979323846;
    Solid result;
    result.impl_->manifold = impl_->manifold.Rotate(0.0, 0.0, degrees);
    return result;
}

Solid Solid::scale(float factor) const {
    Solid result;
    result.impl_->manifold = impl_->manifold.Scale(toMVec3(factor, factor, factor));
    return result;
}

Solid Solid::scale(float x, float y, float z) const {
    Solid result;
    result.impl_->manifold = impl_->manifold.Scale(toMVec3(x, y, z));
    return result;
}

Solid Solid::scale(glm::vec3 factors) const {
    return scale(factors.x, factors.y, factors.z);
}

Solid Solid::transform(const glm::mat4& matrix) const {
    Solid result;
    // Build manifold's mat3x4 from glm::mat4
    // mat3x4 is 3 rows x 4 columns (each column is a vec3)
    // We take the top 3 rows of the glm::mat4
    manifold::mat3x4 mat34(
        manifold::vec3(matrix[0][0], matrix[0][1], matrix[0][2]),  // column 0
        manifold::vec3(matrix[1][0], matrix[1][1], matrix[1][2]),  // column 1
        manifold::vec3(matrix[2][0], matrix[2][1], matrix[2][2]),  // column 2
        manifold::vec3(matrix[3][0], matrix[3][1], matrix[3][2])   // column 3 (translation)
    );

    result.impl_->manifold = impl_->manifold.Transform(mat34);
    return result;
}

Solid Solid::mirror(glm::vec3 normal) const {
    // Create reflection matrix
    glm::vec3 n = glm::normalize(normal);
    glm::mat4 reflect(1.0f);
    reflect[0][0] = 1.0f - 2.0f * n.x * n.x;
    reflect[0][1] = -2.0f * n.x * n.y;
    reflect[0][2] = -2.0f * n.x * n.z;
    reflect[1][0] = -2.0f * n.y * n.x;
    reflect[1][1] = 1.0f - 2.0f * n.y * n.y;
    reflect[1][2] = -2.0f * n.y * n.z;
    reflect[2][0] = -2.0f * n.z * n.x;
    reflect[2][1] = -2.0f * n.z * n.y;
    reflect[2][2] = 1.0f - 2.0f * n.z * n.z;

    return transform(reflect);
}

// === OUTPUT ===

CSGMesh Solid::toMesh() const {
    CSGMesh result;

    manifold::MeshGL mesh = impl_->manifold.GetMeshGL();

    if (mesh.NumVert() == 0 || mesh.NumTri() == 0) {
        return result;
    }

    // Get number of properties per vertex (at least 3 for position)
    int numProp = mesh.numProp;

    // Extract vertices
    result.vertices.reserve(mesh.NumVert());

    // First pass: accumulate face normals per vertex
    std::vector<glm::vec3> vertexNormals(mesh.NumVert(), glm::vec3(0.0f));

    for (size_t t = 0; t < mesh.NumTri(); ++t) {
        uint32_t i0 = mesh.triVerts[t * 3 + 0];
        uint32_t i1 = mesh.triVerts[t * 3 + 1];
        uint32_t i2 = mesh.triVerts[t * 3 + 2];

        glm::vec3 p0(mesh.vertProperties[i0 * numProp + 0],
                     mesh.vertProperties[i0 * numProp + 1],
                     mesh.vertProperties[i0 * numProp + 2]);
        glm::vec3 p1(mesh.vertProperties[i1 * numProp + 0],
                     mesh.vertProperties[i1 * numProp + 1],
                     mesh.vertProperties[i1 * numProp + 2]);
        glm::vec3 p2(mesh.vertProperties[i2 * numProp + 0],
                     mesh.vertProperties[i2 * numProp + 1],
                     mesh.vertProperties[i2 * numProp + 2]);

        glm::vec3 faceNormal = glm::normalize(glm::cross(p1 - p0, p2 - p0));

        if (!std::isnan(faceNormal.x)) {
            vertexNormals[i0] += faceNormal;
            vertexNormals[i1] += faceNormal;
            vertexNormals[i2] += faceNormal;
        }
    }

    // Normalize accumulated normals
    for (auto& n : vertexNormals) {
        float len = glm::length(n);
        if (len > 0.0001f) {
            n /= len;
        } else {
            n = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    // Create vertices with normals and basic UVs
    const float PI = 3.14159265359f;
    for (size_t i = 0; i < mesh.NumVert(); ++i) {
        Vertex3D v;
        v.position = glm::vec3(
            mesh.vertProperties[i * numProp + 0],
            mesh.vertProperties[i * numProp + 1],
            mesh.vertProperties[i * numProp + 2]
        );
        v.normal = vertexNormals[i];

        // Simple spherical UV mapping based on normal
        glm::vec3 n = glm::normalize(v.normal);
        v.uv.x = 0.5f + std::atan2(n.z, n.x) / (2.0f * PI);
        v.uv.y = 0.5f - std::asin(glm::clamp(n.y, -1.0f, 1.0f)) / PI;

        // Basic tangent
        v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

        result.vertices.push_back(v);
    }

    // Copy indices directly
    result.indices = mesh.triVerts;

    return result;
}

bool Solid::valid() const {
    return impl_->manifold.Status() == manifold::Manifold::Error::NoError &&
           !impl_->manifold.IsEmpty();
}

size_t Solid::triangleCount() const {
    return impl_->manifold.NumTri();
}

// === CONVENIENCE FUNCTIONS ===

Solid roundedBox(float width, float height, float depth, float radius, int segments) {
    // Simple approach: create inner box + cylinders on edges + spheres on corners
    Solid inner = Solid::box(width - 2*radius, height - 2*radius, depth - 2*radius);

    // Add face slabs
    Solid topBot = Solid::box(width - 2*radius, radius*2, depth - 2*radius);
    Solid frontBack = Solid::box(width - 2*radius, height - 2*radius, radius*2);
    Solid leftRight = Solid::box(radius*2, height - 2*radius, depth - 2*radius);

    inner = inner + topBot.translate(0, (height-radius)/2 - radius/2, 0);
    inner = inner + topBot.translate(0, -(height-radius)/2 + radius/2, 0);
    inner = inner + frontBack.translate(0, 0, (depth-radius)/2 - radius/2);
    inner = inner + frontBack.translate(0, 0, -(depth-radius)/2 + radius/2);
    inner = inner + leftRight.translate((width-radius)/2 - radius/2, 0, 0);
    inner = inner + leftRight.translate(-(width-radius)/2 + radius/2, 0, 0);

    // Add corner spheres
    float hx = width/2 - radius;
    float hy = height/2 - radius;
    float hz = depth/2 - radius;

    Solid corner = Solid::sphere(radius, segments);
    for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
            for (int sz : {-1, 1}) {
                inner = inner + corner.translate(sx * hx, sy * hy, sz * hz);
            }
        }
    }

    // Add edge cylinders
    const float PI = 3.14159265359f;
    Solid edgeX = Solid::cylinder(radius, width - 2*radius, segments).rotateZ(PI/2);
    Solid edgeY = Solid::cylinder(radius, height - 2*radius, segments);
    Solid edgeZ = Solid::cylinder(radius, depth - 2*radius, segments).rotateX(PI/2);

    for (int sy : {-1, 1}) {
        for (int sz : {-1, 1}) {
            inner = inner + edgeX.translate(0, sy * hy, sz * hz);
        }
    }
    for (int sx : {-1, 1}) {
        for (int sz : {-1, 1}) {
            inner = inner + edgeY.translate(sx * hx, 0, sz * hz);
        }
    }
    for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
            inner = inner + edgeZ.translate(sx * hx, sy * hy, 0);
        }
    }

    return inner;
}

Solid hexPrism(float radius, float height) {
    // Build hexagonal prism from triangular mesh
    std::vector<float> vertProperties;
    std::vector<uint32_t> triVerts;

    const float PI = 3.14159265359f;
    float h2 = height / 2.0f;

    // Top and bottom center
    vertProperties.push_back(0); vertProperties.push_back(h2); vertProperties.push_back(0);   // 0: top center
    vertProperties.push_back(0); vertProperties.push_back(-h2); vertProperties.push_back(0);  // 1: bottom center

    // Hex vertices
    for (int i = 0; i < 6; ++i) {
        float angle = PI / 3.0f * i;
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);
        vertProperties.push_back(x); vertProperties.push_back(h2); vertProperties.push_back(z);   // 2+i*2: top
        vertProperties.push_back(x); vertProperties.push_back(-h2); vertProperties.push_back(z);  // 3+i*2: bottom
    }

    // Top face (fan from center)
    for (int i = 0; i < 6; ++i) {
        int next = (i + 1) % 6;
        triVerts.push_back(0);
        triVerts.push_back(2 + i*2);
        triVerts.push_back(2 + next*2);
    }

    // Bottom face (fan from center, reversed winding)
    for (int i = 0; i < 6; ++i) {
        int next = (i + 1) % 6;
        triVerts.push_back(1);
        triVerts.push_back(3 + next*2);
        triVerts.push_back(3 + i*2);
    }

    // Side faces
    for (int i = 0; i < 6; ++i) {
        int next = (i + 1) % 6;
        uint32_t topCurr = 2 + i*2;
        uint32_t topNext = 2 + next*2;
        uint32_t botCurr = 3 + i*2;
        uint32_t botNext = 3 + next*2;

        triVerts.push_back(topCurr);
        triVerts.push_back(botCurr);
        triVerts.push_back(botNext);

        triVerts.push_back(topCurr);
        triVerts.push_back(botNext);
        triVerts.push_back(topNext);
    }

    manifold::MeshGL mesh;
    mesh.numProp = 3;
    mesh.vertProperties = vertProperties;
    mesh.triVerts = triVerts;

    return solidFromMeshGL(mesh);
}

Solid wedge(float width, float height, float depth) {
    // Wedge: rectangular base, triangular profile
    std::vector<float> vertProperties = {
        -width/2, 0, -depth/2,      // 0: front-left bottom
        width/2, 0, -depth/2,       // 1: front-right bottom
        width/2, 0, depth/2,        // 2: back-right bottom
        -width/2, 0, depth/2,       // 3: back-left bottom
        -width/2, height, depth/2,  // 4: back-left top
        width/2, height, depth/2,   // 5: back-right top
    };

    std::vector<uint32_t> triVerts = {
        // Bottom
        0, 2, 1,
        0, 3, 2,
        // Back
        3, 4, 5,
        3, 5, 2,
        // Left side (triangle)
        0, 4, 3,
        // Right side (triangle)
        1, 2, 5,
        // Front slope
        0, 1, 5,
        0, 5, 4,
    };

    manifold::MeshGL mesh;
    mesh.numProp = 3;
    mesh.vertProperties = vertProperties;
    mesh.triVerts = triVerts;

    return solidFromMeshGL(mesh);
}

Solid linearArray(const Solid& base, glm::vec3 spacing, int count) {
    if (count <= 0) return Solid();
    if (count == 1) return base;

    Solid result = base;
    for (int i = 1; i < count; ++i) {
        result = result + base.translate(spacing * static_cast<float>(i));
    }
    return result;
}

Solid radialArray(const Solid& base, int count, float radius) {
    if (count <= 0) return Solid();
    if (count == 1) return base.translate(radius, 0, 0);

    const float PI = 3.14159265359f;
    Solid result;

    for (int i = 0; i < count; ++i) {
        float angle = 2.0f * PI * i / count;
        Solid copy = base
            .translate(radius, 0, 0)
            .rotateY(angle);

        if (i == 0) {
            result = copy;
        } else {
            result = result + copy;
        }
    }
    return result;
}

} // namespace vivid::csg
