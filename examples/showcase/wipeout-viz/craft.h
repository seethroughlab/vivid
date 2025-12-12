// Wipeout 2029 - Procedural Craft Classes
// Modular craft geometry with customizable parts

#pragma once

#include <vivid/render3d/mesh_builder.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace vivid::render3d;

// =============================================================================
// Base Class for Craft Parts
// =============================================================================

class CraftPart {
public:
    virtual ~CraftPart() = default;

    /// Build the mesh for this part
    virtual MeshBuilder build() const = 0;

    /// Get the part's local transform offset
    glm::vec3 offset() const { return m_offset; }

    /// Set the part's local transform offset
    void setOffset(glm::vec3 offset) { m_offset = offset; }

protected:
    glm::vec3 m_offset{0, 0, 0};
};

// =============================================================================
// Fuselage - Main body of the craft
// =============================================================================

class Fuselage : public CraftPart {
public:
    float length = 1.4f;
    float height = 0.16f;
    float width = 0.32f;

    // Nose parameters
    float noseWidth = 0.32f;
    float noseLength = 0.7f;
    int noseSides = 4;

    // Tail parameters
    float tailBottomRadius = 0.16f;
    float tailTopRadius = 0.08f;
    float tailLength = 0.35f;

    MeshBuilder build() const override {
        // Main body box
        auto body = MeshBuilder::box(length, height, width);

        // Angular nose - pyramid pointing forward
        auto nose = MeshBuilder::pyramid(noseWidth, noseLength, noseSides)
            .rotate(glm::radians(-90.0f), {0, 0, 1})
            .translate({length * 0.5f + noseLength * 0.5f, 0, 0});
        body.append(nose);

        // Rear taper - frustum narrowing toward back
        auto tail = MeshBuilder::frustum(tailBottomRadius, tailTopRadius, tailLength, 6)
            .rotate(glm::radians(90.0f), {0, 0, 1})
            .translate({-length * 0.5f - tailLength * 0.5f, 0, 0});
        body.append(tail);

        // Spine ridge on top
        auto spine = MeshBuilder::box(length * 0.6f, 0.025f, 0.06f)
            .translate({-0.05f, height * 0.5f + 0.0125f, 0});
        body.append(spine);

        return body;
    }
};

// =============================================================================
// Cockpit - Pilot canopy and windscreen
// =============================================================================

class Cockpit : public CraftPart {
public:
    float baseLength = 0.35f;
    float baseHeight = 0.08f;
    float baseWidth = 0.2f;

    float windscreenLength = 0.2f;
    float windscreenHeight = 0.1f;
    float windscreenWidth = 0.18f;

    Cockpit() {
        m_offset = {0.2f, 0.12f, 0};
    }

    MeshBuilder build() const override {
        // Cockpit base
        auto cockpit = MeshBuilder::box(baseLength, baseHeight, baseWidth)
            .translate(m_offset);

        // Sloped windscreen (wedge rotated to slope backward)
        auto windscreen = MeshBuilder::wedge(windscreenLength, windscreenHeight, windscreenWidth)
            .rotate(glm::radians(180.0f), {0, 1, 0})
            .translate({m_offset.x + baseLength * 0.5f + windscreenLength * 0.1f, m_offset.y, m_offset.z});
        cockpit.append(windscreen);

        return cockpit;
    }
};

// =============================================================================
// SidePod - Engine nacelle (left or right)
// =============================================================================

class SidePod : public CraftPart {
public:
    float bodyBottomRadius = 0.11f;
    float bodyTopRadius = 0.09f;
    float bodyLength = 1.1f;
    int bodySegments = 8;

    float noseWidth = 0.18f;
    float noseLength = 0.3f;

    float exhaustBottomRadius = 0.07f;
    float exhaustTopRadius = 0.1f;
    float exhaustLength = 0.15f;

    bool isRight = false;  // Mirror for right side

    SidePod(bool right = false) : isRight(right) {
        float zOffset = right ? -0.4f : 0.4f;
        m_offset = {-0.05f, -0.03f, zOffset};
    }

    MeshBuilder build() const override {
        // Main pod body - frustum for tapered look
        auto pod = MeshBuilder::frustum(bodyBottomRadius, bodyTopRadius, bodyLength, bodySegments)
            .rotate(glm::radians(90.0f), {0, 0, 1})
            .translate(m_offset);

        // Pod nose - pyramid for sharp angular front
        auto nose = MeshBuilder::pyramid(noseWidth, noseLength, 4)
            .rotate(glm::radians(-90.0f), {0, 0, 1})
            .translate({m_offset.x + bodyLength * 0.5f + noseLength * 0.1f, m_offset.y, m_offset.z});
        pod.append(nose);

        // Engine exhaust - frustum opening up at rear
        auto exhaust = MeshBuilder::frustum(exhaustBottomRadius, exhaustTopRadius, exhaustLength, 6)
            .rotate(glm::radians(90.0f), {0, 0, 1})
            .translate({m_offset.x - bodyLength * 0.5f - exhaustLength * 0.15f, m_offset.y, m_offset.z});
        pod.append(exhaust);

        // Intake scoop on top
        auto intake = MeshBuilder::wedge(0.12f, 0.04f, 0.08f)
            .translate({m_offset.x + bodyLength * 0.25f, m_offset.y + bodyTopRadius + 0.02f, m_offset.z});
        pod.append(intake);

        return pod;
    }
};

// =============================================================================
// Strut - Connecting strut between fuselage and pod
// =============================================================================

class Strut : public CraftPart {
public:
    float length = 0.25f;
    float height = 0.06f;
    float width = 0.12f;
    float angle = 5.0f;  // Downward angle in degrees

    bool isRight = false;

    Strut(bool right = false) : isRight(right) {
        float zOffset = right ? -0.26f : 0.26f;
        m_offset = {0.1f, 0, zOffset};
    }

    MeshBuilder build() const override {
        float yawAngle = isRight ? -90.0f : 90.0f;
        float rollAngle = isRight ? angle : -angle;

        auto strut = MeshBuilder::wedge(length, height, width)
            .rotate(glm::radians(yawAngle), {0, 1, 0})
            .rotate(glm::radians(rollAngle), {1, 0, 0})
            .translate(m_offset);

        return strut;
    }
};

// =============================================================================
// RearWing - Main rear wing with endplates
// =============================================================================

class RearWing : public CraftPart {
public:
    float wingLength = 0.22f;
    float wingHeight = 0.018f;
    float wingWidth = 0.9f;

    float endplateLength = 0.2f;
    float endplateHeight = 0.14f;
    float endplateWidth = 0.02f;

    RearWing() {
        m_offset = {-0.75f, 0.18f, 0};
    }

    MeshBuilder build() const override {
        auto wing = MeshBuilder::box(wingLength, wingHeight, wingWidth)
            .translate(m_offset);

        // Wing endplates - wedge-shaped for swept look
        auto leftEndplate = MeshBuilder::wedge(endplateLength, endplateHeight, endplateWidth)
            .rotate(glm::radians(-90.0f), {0, 0, 1})
            .translate({m_offset.x, m_offset.y + 0.02f, wingWidth * 0.5f});
        wing.append(leftEndplate);

        auto rightEndplate = MeshBuilder::wedge(endplateLength, endplateHeight, endplateWidth)
            .rotate(glm::radians(-90.0f), {0, 0, 1})
            .translate({m_offset.x, m_offset.y + 0.02f, -wingWidth * 0.5f});
        wing.append(rightEndplate);

        return wing;
    }
};

// =============================================================================
// VerticalFin - Rear vertical stabilizer
// =============================================================================

class VerticalFin : public CraftPart {
public:
    float baseWidth = 0.04f;
    float height = 0.32f;
    int sides = 4;
    glm::vec3 stretch = {4.0f, 1.0f, 1.0f};
    float leanAngle = 15.0f;  // Backward lean in degrees

    VerticalFin() {
        m_offset = {-0.65f, 0.28f, 0};
    }

    MeshBuilder build() const override {
        auto fin = MeshBuilder::pyramid(baseWidth, height, sides)
            .scale(stretch)
            .rotate(glm::radians(leanAngle), {0, 0, 1})
            .translate(m_offset);

        return fin;
    }
};

// =============================================================================
// Canard - Front delta wing (left or right)
// =============================================================================

class Canard : public CraftPart {
public:
    float baseRadius = 0.15f;
    float height = 0.02f;
    glm::vec3 stretch = {1.5f, 1.0f, 1.0f};
    float sweepAngle = 20.0f;  // Sweep back angle

    bool isRight = false;

    Canard(bool right = false) : isRight(right) {
        float zOffset = right ? -0.22f : 0.22f;
        m_offset = {0.65f, 0.02f, zOffset};
    }

    MeshBuilder build() const override {
        float sweep = isRight ? sweepAngle : -sweepAngle;

        auto canard = MeshBuilder::pyramid(baseRadius, height, 3)  // Triangle base
            .scale(stretch)
            .rotate(glm::radians(-90.0f), {1, 0, 0})  // Lay flat
            .rotate(glm::radians(sweep), {0, 1, 0})   // Sweep back
            .translate(m_offset);

        return canard;
    }
};

// =============================================================================
// AirIntake - Wedge scoop on fuselage
// =============================================================================

class AirIntake : public CraftPart {
public:
    float length = 0.14f;
    float height = 0.06f;
    float width = 0.07f;

    bool isRight = false;

    AirIntake(bool right = false) : isRight(right) {
        float zOffset = right ? -0.12f : 0.12f;
        m_offset = {-0.1f, 0.09f, zOffset};
    }

    MeshBuilder build() const override {
        auto intake = MeshBuilder::wedge(length, height, width)
            .translate(m_offset);

        // Add intake grille recess
        auto recess = MeshBuilder::box(length * 0.6f, height * 0.4f, 0.01f)
            .translate({m_offset.x + length * 0.2f, m_offset.y + height * 0.2f, m_offset.z});
        intake.subtract(recess);

        return intake;
    }
};

// =============================================================================
// HoverPad - Underside anti-gravity generator
// =============================================================================

class HoverPad : public CraftPart {
public:
    float outerRadius = 0.08f;
    float innerRadius = 0.04f;
    float height = 0.04f;
    int segments = 8;

    bool isRight = false;
    bool isFront = false;

    HoverPad(bool right = false, bool front = false) : isRight(right), isFront(front) {
        float xOffset = front ? 0.4f : -0.35f;
        float zOffset = right ? -0.15f : 0.15f;
        m_offset = {xOffset, -0.09f, zOffset};
    }

    MeshBuilder build() const override {
        // Outer housing - octagonal frustum
        auto pad = MeshBuilder::frustum(outerRadius, outerRadius * 0.9f, height, segments)
            .rotate(glm::radians(180.0f), {1, 0, 0})  // Point downward
            .translate(m_offset);

        // Inner glow ring recess
        auto innerRecess = MeshBuilder::cylinder(innerRadius, height * 0.5f, segments)
            .translate({m_offset.x, m_offset.y - height * 0.25f, m_offset.z});
        pad.subtract(innerRecess);

        // Outer detail ring
        auto outerRing = MeshBuilder::torus(outerRadius * 0.85f, 0.008f, segments, 6)
            .translate({m_offset.x, m_offset.y - height * 0.3f, m_offset.z});
        pad.add(outerRing);

        return pad;
    }
};

// =============================================================================
// EngineGlow - Emissive engine exhaust effect
// =============================================================================

class EngineGlow : public CraftPart {
public:
    float innerRadius = 0.045f;
    float outerRadius = 0.075f;
    float length = 0.15f;
    int segments = 8;

    bool isRight = false;

    EngineGlow(bool right = false) : isRight(right) {
        float zOffset = right ? -0.42f : 0.42f;
        m_offset = {-0.78f, -0.02f, zOffset};
    }

    MeshBuilder build() const override {
        // Inner bright core - small frustum
        auto glow = MeshBuilder::frustum(innerRadius, innerRadius * 0.4f, length, segments)
            .rotate(glm::radians(90.0f), {0, 0, 1})
            .translate(m_offset);

        // Outer glow ring - larger frustum around the core
        auto outer = MeshBuilder::frustum(outerRadius, outerRadius * 0.25f, length * 0.7f, segments)
            .rotate(glm::radians(90.0f), {0, 0, 1})
            .translate({m_offset.x + length * 0.15f, m_offset.y, m_offset.z});
        glow.append(outer);

        return glow;
    }
};

// =============================================================================
// Craft - Complete anti-gravity racing craft
// =============================================================================

class Craft {
public:
    // Component parts
    Fuselage fuselage;
    Cockpit cockpit;
    SidePod leftPod{false};
    SidePod rightPod{true};
    Strut leftStrut{false};
    Strut rightStrut{true};
    RearWing rearWing;
    VerticalFin verticalFin;
    Canard leftCanard{false};
    Canard rightCanard{true};
    AirIntake leftIntake{false};
    AirIntake rightIntake{true};

    // Underside hover pads
    HoverPad frontLeftPad{false, true};
    HoverPad frontRightPad{true, true};
    HoverPad rearLeftPad{false, false};
    HoverPad rearRightPad{true, false};

    // Engine glow parts (separate for emissive material)
    EngineGlow leftGlow{false};
    EngineGlow rightGlow{true};

    /// Build the complete craft mesh (body only, no glow)
    MeshBuilder build() const {
        auto mesh = fuselage.build();

        mesh.append(cockpit.build());
        mesh.append(leftPod.build());
        mesh.append(rightPod.build());
        mesh.append(leftStrut.build());
        mesh.append(rightStrut.build());
        mesh.append(rearWing.build());
        mesh.append(verticalFin.build());
        mesh.append(leftCanard.build());
        mesh.append(rightCanard.build());
        mesh.append(leftIntake.build());
        mesh.append(rightIntake.build());

        // Underside anti-gravity hover pads
        mesh.append(frontLeftPad.build());
        mesh.append(frontRightPad.build());
        mesh.append(rearLeftPad.build());
        mesh.append(rearRightPad.build());

        // Apply flat normals for faceted PS1 look
        mesh.computeFlatNormals();

        return mesh;
    }

    /// Build the engine glow mesh (for emissive material)
    MeshBuilder buildEngineGlow() const {
        auto mesh = leftGlow.build();
        mesh.append(rightGlow.build());
        mesh.computeFlatNormals();
        return mesh;
    }
};
