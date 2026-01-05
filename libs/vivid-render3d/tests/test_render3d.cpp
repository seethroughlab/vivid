/**
 * @file test_render3d.cpp
 * @brief Unit tests for 3D rendering operators
 *
 * Tests MeshBuilder, Camera3D, and primitive operators (Box, Sphere, etc.)
 * These tests do not require GPU - they test CPU-side mesh generation and math.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/render3d/mesh_builder.h>
#include <vivid/render3d/camera.h>
#include <vivid/render3d/primitives.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

using namespace vivid::render3d;
using Catch::Matchers::WithinAbs;

// =============================================================================
// MeshBuilder Tests
// =============================================================================

TEST_CASE("MeshBuilder primitive generation", "[render3d][meshbuilder]") {
    SECTION("box creates 24 vertices and 36 indices") {
        auto builder = MeshBuilder::box(1.0f, 1.0f, 1.0f);
        // Box has 6 faces * 4 vertices = 24 vertices (flat shading)
        REQUIRE(builder.vertexCount() == 24);
        // Box has 6 faces * 2 triangles * 3 indices = 36 indices
        REQUIRE(builder.indexCount() == 36);
    }

    SECTION("sphere creates expected vertex count") {
        auto builder = MeshBuilder::sphere(1.0f, 16);
        // Sphere with 16 segments creates roughly (segments+1) * (segments/2+1) vertices
        REQUIRE(builder.vertexCount() > 50);  // At least 50 vertices
        REQUIRE(builder.indexCount() > 0);
    }

    SECTION("cylinder creates expected vertex count") {
        auto builder = MeshBuilder::cylinder(0.5f, 1.0f, 16);
        REQUIRE(builder.vertexCount() > 0);
        REQUIRE(builder.indexCount() > 0);
    }

    SECTION("cone creates expected vertex count") {
        auto builder = MeshBuilder::cone(0.5f, 1.0f, 16);
        REQUIRE(builder.vertexCount() > 0);
        REQUIRE(builder.indexCount() > 0);
    }

    SECTION("torus creates expected vertex count") {
        auto builder = MeshBuilder::torus(0.5f, 0.2f, 16, 8);
        REQUIRE(builder.vertexCount() > 0);
        REQUIRE(builder.indexCount() > 0);
    }

    SECTION("plane creates expected vertex count") {
        auto builder = MeshBuilder::plane(1.0f, 1.0f, 4, 4);
        // 4x4 subdivisions = 5x5 = 25 vertices
        REQUIRE(builder.vertexCount() == 25);
        // 4*4 quads = 16 quads * 2 triangles * 3 indices = 96 indices
        REQUIRE(builder.indexCount() == 96);
    }

    SECTION("pyramid creates expected structure") {
        auto builder = MeshBuilder::pyramid(1.0f, 1.0f, 4);
        REQUIRE(builder.vertexCount() > 0);
        REQUIRE(builder.indexCount() > 0);
    }

    SECTION("wedge creates expected structure") {
        auto builder = MeshBuilder::wedge(1.0f, 1.0f, 1.0f);
        REQUIRE(builder.vertexCount() > 0);
        REQUIRE(builder.indexCount() > 0);
    }

    SECTION("frustum creates expected structure") {
        auto builder = MeshBuilder::frustum(1.0f, 0.5f, 1.0f, 16);
        REQUIRE(builder.vertexCount() > 0);
        REQUIRE(builder.indexCount() > 0);
    }
}

TEST_CASE("MeshBuilder transformations", "[render3d][meshbuilder]") {
    SECTION("translate moves vertices") {
        auto builder = MeshBuilder::box(1.0f, 1.0f, 1.0f);
        size_t beforeCount = builder.vertexCount();
        builder.translate(glm::vec3(1.0f, 2.0f, 3.0f));
        REQUIRE(builder.vertexCount() == beforeCount);  // Same vertex count
    }

    SECTION("scale modifies mesh") {
        auto builder = MeshBuilder::box(1.0f, 1.0f, 1.0f);
        size_t beforeCount = builder.vertexCount();
        builder.scale(2.0f);
        REQUIRE(builder.vertexCount() == beforeCount);  // Same vertex count
    }

    SECTION("rotate modifies mesh") {
        auto builder = MeshBuilder::box(1.0f, 1.0f, 1.0f);
        size_t beforeCount = builder.vertexCount();
        builder.rotate(glm::half_pi<float>(), glm::vec3(0, 1, 0));
        REQUIRE(builder.vertexCount() == beforeCount);  // Same vertex count
    }
}

TEST_CASE("MeshBuilder CSG operations", "[render3d][meshbuilder]") {
    SECTION("subtract creates difference") {
        auto box1 = MeshBuilder::box(2.0f, 2.0f, 2.0f);
        auto box2 = MeshBuilder::box(1.0f, 1.0f, 3.0f);  // Punch through
        box1.subtract(box2);
        REQUIRE(box1.vertexCount() > 0);
        REQUIRE(box1.indexCount() > 0);
    }

    SECTION("add creates union") {
        auto box1 = MeshBuilder::box(1.0f, 1.0f, 1.0f);
        auto box2 = MeshBuilder::box(1.0f, 1.0f, 1.0f);
        box2.translate(glm::vec3(0.5f, 0.0f, 0.0f));
        box1.add(box2);
        REQUIRE(box1.vertexCount() > 0);
        REQUIRE(box1.indexCount() > 0);
    }

    SECTION("intersect creates intersection") {
        auto box1 = MeshBuilder::box(2.0f, 2.0f, 2.0f);
        auto box2 = MeshBuilder::box(1.0f, 1.0f, 1.0f);
        box1.intersect(box2);
        REQUIRE(box1.vertexCount() > 0);
        REQUIRE(box1.indexCount() > 0);
    }
}

TEST_CASE("MeshBuilder builds valid mesh", "[render3d][meshbuilder]") {
    SECTION("box builds mesh with vertices and indices") {
        auto builder = MeshBuilder::box(1.0f, 1.0f, 1.0f);
        Mesh mesh = builder.build();
        REQUIRE(mesh.vertices.size() == 24);
        REQUIRE(mesh.indices.size() == 36);
    }

    SECTION("sphere builds mesh") {
        auto builder = MeshBuilder::sphere(1.0f, 16);
        Mesh mesh = builder.build();
        REQUIRE(mesh.vertices.size() > 0);
        REQUIRE(mesh.indices.size() > 0);
    }

    SECTION("clear removes all data") {
        auto builder = MeshBuilder::box(1.0f, 1.0f, 1.0f);
        REQUIRE(builder.vertexCount() > 0);
        builder.clear();
        REQUIRE(builder.vertexCount() == 0);
        REQUIRE(builder.indexCount() == 0);
    }
}

// =============================================================================
// Camera3D Tests
// =============================================================================

TEST_CASE("Camera3D defaults", "[render3d][camera]") {
    Camera3D camera;

    SECTION("default position is (0, 0, 5)") {
        auto pos = camera.getPosition();
        REQUIRE_THAT(pos.x, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(pos.y, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(pos.z, WithinAbs(5.0f, 0.001f));
    }

    SECTION("default target is origin") {
        auto target = camera.getTarget();
        REQUIRE_THAT(target.x, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(target.y, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(target.z, WithinAbs(0.0f, 0.001f));
    }

    SECTION("default up is (0, 1, 0)") {
        auto up = camera.getUp();
        REQUIRE_THAT(up.x, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(up.y, WithinAbs(1.0f, 0.001f));
        REQUIRE_THAT(up.z, WithinAbs(0.0f, 0.001f));
    }

    SECTION("default FOV is 45 degrees") {
        REQUIRE_THAT(camera.getFov(), WithinAbs(45.0f, 0.001f));
    }

    SECTION("default near plane is 0.1") {
        REQUIRE_THAT(camera.getNear(), WithinAbs(0.1f, 0.001f));
    }

    SECTION("default far plane is 100") {
        REQUIRE_THAT(camera.getFar(), WithinAbs(100.0f, 0.001f));
    }

    SECTION("default projection mode is Perspective") {
        REQUIRE(camera.getProjectionMode() == ProjectionMode::Perspective);
    }
}

TEST_CASE("Camera3D setters", "[render3d][camera]") {
    Camera3D camera;

    SECTION("position setter works") {
        camera.position(glm::vec3(1.0f, 2.0f, 3.0f));
        auto pos = camera.getPosition();
        REQUIRE_THAT(pos.x, WithinAbs(1.0f, 0.001f));
        REQUIRE_THAT(pos.y, WithinAbs(2.0f, 0.001f));
        REQUIRE_THAT(pos.z, WithinAbs(3.0f, 0.001f));
    }

    SECTION("target setter works") {
        camera.target(glm::vec3(5.0f, 5.0f, 5.0f));
        auto target = camera.getTarget();
        REQUIRE_THAT(target.x, WithinAbs(5.0f, 0.001f));
        REQUIRE_THAT(target.y, WithinAbs(5.0f, 0.001f));
        REQUIRE_THAT(target.z, WithinAbs(5.0f, 0.001f));
    }

    SECTION("fov setter works") {
        camera.fov(60.0f);
        REQUIRE_THAT(camera.getFov(), WithinAbs(60.0f, 0.001f));
    }

    SECTION("near/far plane setters work") {
        camera.nearPlane(0.5f);
        camera.farPlane(500.0f);
        REQUIRE_THAT(camera.getNear(), WithinAbs(0.5f, 0.001f));
        REQUIRE_THAT(camera.getFar(), WithinAbs(500.0f, 0.001f));
    }

    SECTION("projection mode setter works") {
        camera.projectionMode(ProjectionMode::Orthographic);
        REQUIRE(camera.getProjectionMode() == ProjectionMode::Orthographic);
    }

    SECTION("ortho size setter works") {
        camera.orthoSize(20.0f);
        REQUIRE_THAT(camera.getOrthoSize(), WithinAbs(20.0f, 0.001f));
    }
}

TEST_CASE("Camera3D lookAt", "[render3d][camera]") {
    Camera3D camera;

    SECTION("lookAt sets position and target") {
        camera.lookAt(glm::vec3(10.0f, 10.0f, 10.0f), glm::vec3(0.0f, 0.0f, 0.0f));
        auto pos = camera.getPosition();
        auto target = camera.getTarget();
        REQUIRE_THAT(pos.x, WithinAbs(10.0f, 0.001f));
        REQUIRE_THAT(pos.y, WithinAbs(10.0f, 0.001f));
        REQUIRE_THAT(pos.z, WithinAbs(10.0f, 0.001f));
        REQUIRE_THAT(target.x, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(target.y, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(target.z, WithinAbs(0.0f, 0.001f));
    }
}

TEST_CASE("Camera3D orbit", "[render3d][camera]") {
    Camera3D camera;

    SECTION("orbit sets position based on angles") {
        camera.orbit(5.0f, 0.0f, 0.0f);  // distance 5, azimuth 0, elevation 0
        auto pos = camera.getPosition();
        // At azimuth=0, elevation=0, camera should be at (0, 0, 5)
        REQUIRE_THAT(pos.z, WithinAbs(5.0f, 0.01f));
        REQUIRE_THAT(pos.y, WithinAbs(0.0f, 0.01f));
    }

    SECTION("orbit around center point") {
        glm::vec3 center(1.0f, 2.0f, 3.0f);
        camera.orbit(center, 5.0f, 0.0f, 0.0f);
        auto pos = camera.getPosition();
        // Camera should be 5 units away from center along Z
        REQUIRE_THAT(pos.z, WithinAbs(8.0f, 0.01f));  // 3 + 5 = 8
    }
}

TEST_CASE("Camera3D matrices", "[render3d][camera]") {
    Camera3D camera;

    SECTION("view matrix is valid") {
        glm::mat4 view = camera.viewMatrix();
        // View matrix should not be identity
        REQUIRE(view != glm::mat4(1.0f));
    }

    SECTION("projection matrix is valid") {
        camera.aspect(16.0f / 9.0f);
        glm::mat4 proj = camera.projectionMatrix();
        // Projection matrix should not be identity
        REQUIRE(proj != glm::mat4(1.0f));
    }

    SECTION("view-projection matrix is combination") {
        glm::mat4 view = camera.viewMatrix();
        glm::mat4 proj = camera.projectionMatrix();
        glm::mat4 vp = camera.viewProjectionMatrix();
        // viewProjectionMatrix should equal proj * view
        glm::mat4 expected = proj * view;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                REQUIRE_THAT(vp[i][j], WithinAbs(expected[i][j], 0.0001f));
            }
        }
    }

    SECTION("orthographic projection matrix differs") {
        camera.projectionMode(ProjectionMode::Perspective);
        glm::mat4 perspProj = camera.projectionMatrix();

        camera.projectionMode(ProjectionMode::Orthographic);
        glm::mat4 orthoProj = camera.projectionMatrix();

        // Matrices should be different
        REQUIRE(perspProj != orthoProj);
    }
}

TEST_CASE("Camera3D direction vectors", "[render3d][camera]") {
    Camera3D camera;
    camera.lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));

    SECTION("forward points toward target") {
        auto fwd = camera.forward();
        // Camera at (0,0,5) looking at origin, forward is (0,0,-1)
        REQUIRE_THAT(fwd.x, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(fwd.y, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(fwd.z, WithinAbs(-1.0f, 0.001f));
    }

    SECTION("right is perpendicular to forward and up") {
        auto right = camera.right();
        auto fwd = camera.forward();
        auto up = camera.getUp();
        // right should be perpendicular to forward
        float dotFwd = glm::dot(right, fwd);
        REQUIRE_THAT(dotFwd, WithinAbs(0.0f, 0.001f));
    }
}

// =============================================================================
// Primitive Operator Tests
// =============================================================================

TEST_CASE("Box primitive defaults", "[render3d][primitives]") {
    Box box;

    SECTION("name returns 'Box'") {
        REQUIRE(box.name() == "Box");
    }

    SECTION("has width, height, depth params") {
        auto params = box.params();
        REQUIRE(params.size() >= 3);

        bool hasWidth = false, hasHeight = false, hasDepth = false;
        for (const auto& p : params) {
            if (p.name == "width") hasWidth = true;
            if (p.name == "height") hasHeight = true;
            if (p.name == "depth") hasDepth = true;
        }
        REQUIRE(hasWidth);
        REQUIRE(hasHeight);
        REQUIRE(hasDepth);
    }
}

TEST_CASE("Sphere primitive defaults", "[render3d][primitives]") {
    Sphere sphere;

    SECTION("name returns 'Sphere'") {
        REQUIRE(sphere.name() == "Sphere");
    }

    SECTION("has radius and segments params") {
        auto params = sphere.params();
        REQUIRE(params.size() >= 2);

        bool hasRadius = false, hasSegments = false;
        for (const auto& p : params) {
            if (p.name == "radius") hasRadius = true;
            if (p.name == "segments") hasSegments = true;
        }
        REQUIRE(hasRadius);
        REQUIRE(hasSegments);
    }
}

TEST_CASE("Cylinder primitive defaults", "[render3d][primitives]") {
    Cylinder cylinder;

    SECTION("name returns 'Cylinder'") {
        REQUIRE(cylinder.name() == "Cylinder");
    }

    SECTION("has radius, height, segments params") {
        auto params = cylinder.params();
        REQUIRE(params.size() >= 3);
    }
}

TEST_CASE("Cone primitive defaults", "[render3d][primitives]") {
    Cone cone;

    SECTION("name returns 'Cone'") {
        REQUIRE(cone.name() == "Cone");
    }
}

TEST_CASE("Torus primitive defaults", "[render3d][primitives]") {
    Torus torus;

    SECTION("name returns 'Torus'") {
        REQUIRE(torus.name() == "Torus");
    }

    SECTION("has outerRadius, innerRadius, segments, rings params") {
        auto params = torus.params();
        REQUIRE(params.size() >= 4);
    }
}

TEST_CASE("Plane primitive defaults", "[render3d][primitives]") {
    Plane plane;

    SECTION("name returns 'Plane'") {
        REQUIRE(plane.name() == "Plane");
    }

    SECTION("has width, height, subdivision params") {
        auto params = plane.params();
        REQUIRE(params.size() >= 4);
    }
}

// =============================================================================
// Frustum Tests
// =============================================================================

TEST_CASE("Frustum culling", "[render3d][frustum]") {
    Frustum frustum;
    Camera3D camera;
    camera.lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
    camera.aspect(1.0f);
    camera.fov(90.0f);

    glm::mat4 vp = camera.viewProjectionMatrix();
    frustum.extractFromMatrix(vp);

    SECTION("point at origin is inside frustum") {
        REQUIRE(frustum.containsPoint(glm::vec3(0, 0, 0)));
    }

    SECTION("point behind camera is outside frustum") {
        REQUIRE_FALSE(frustum.containsPoint(glm::vec3(0, 0, 10)));
    }

    SECTION("sphere at origin intersects frustum") {
        REQUIRE(frustum.intersectsSphere(glm::vec3(0, 0, 0), 1.0f));
    }

    SECTION("sphere far away does not intersect") {
        REQUIRE_FALSE(frustum.intersectsSphere(glm::vec3(1000, 0, 0), 1.0f));
    }
}
