#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <limits>

namespace vivid {

/**
 * @brief Standard 3D vertex format supporting normal mapping.
 */
struct Vertex3D {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

/**
 * @brief Axis-aligned bounding box.
 */
struct BoundingBox {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 size() const { return max - min; }
};

/**
 * @brief 3D perspective camera.
 */
class Camera3D {
public:
    glm::vec3 position{0.0f, 0.0f, 5.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    glm::mat4 viewMatrix() const {
        return glm::lookAt(position, target, up);
    }

    glm::mat4 projectionMatrix(float aspectRatio) const {
        return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    }

    glm::mat4 viewProjectionMatrix(float aspectRatio) const {
        return projectionMatrix(aspectRatio) * viewMatrix();
    }

    glm::vec3 forward() const {
        return glm::normalize(target - position);
    }

    void orbit(float yawDelta, float pitchDelta) {
        glm::vec3 offset = position - target;
        float distance = glm::length(offset);
        float theta = std::atan2(offset.x, offset.z);
        float phi = std::acos(offset.y / distance);

        theta += yawDelta;
        phi = glm::clamp(phi + pitchDelta, 0.01f, 3.13f);

        position = target + glm::vec3(
            distance * std::sin(phi) * std::sin(theta),
            distance * std::cos(phi),
            distance * std::sin(phi) * std::cos(theta)
        );
    }

    void zoom(float delta) {
        glm::vec3 offset = position - target;
        float distance = glm::length(offset);
        float newDistance = glm::max(0.1f, distance - delta);
        position = target + glm::normalize(offset) * newDistance;
    }
};

/**
 * @brief Opaque handle to a 3D mesh.
 */
struct Mesh3D {
    void* handle = nullptr;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    BoundingBox bounds;

    bool valid() const { return handle != nullptr; }
};

/**
 * @brief Opaque handle to a 3D render pipeline.
 */
struct Pipeline3D {
    void* handle = nullptr;
    bool valid() const { return handle != nullptr; }
};

// Primitive generators - populate vertex and index buffers
namespace primitives {

void generateCube(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices);

void generatePlane(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                   float width = 1.0f, float height = 1.0f,
                   int subdivisionsX = 1, int subdivisionsZ = 1);

void generateSphere(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                    float radius = 0.5f, int segments = 32, int rings = 16);

void generateCylinder(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                      float radius = 0.5f, float height = 1.0f, int segments = 32);

void generateTorus(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                   float majorRadius = 0.5f, float minorRadius = 0.2f,
                   int majorSegments = 32, int minorSegments = 16);

} // namespace primitives

} // namespace vivid
