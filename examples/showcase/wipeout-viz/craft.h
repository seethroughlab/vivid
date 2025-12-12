// Wipeout 2029 - Procedural Craft Classes
// Delta-wing anti-gravity racing craft based on Wipeout reference designs
//
// Key design principles:
// - Wide, flat delta-wing planform (wingspan > length)
// - Integrated engine nacelles (part of wing, not separate)
// - Very low profile (~0.15 units height)
// - Long needle nose
// - Angular faceted surfaces for PS1 aesthetic

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
// DeltaBody - Main wide triangular body with integrated nacelles
// =============================================================================

class DeltaBody : public CraftPart {
public:
    // Overall dimensions - LONG and NARROW like an arrow/needle
    float length = 2.4f;        // Very long (nose to tail)
    float width = 0.9f;         // Relatively narrow
    float height = 0.12f;       // Low profile

    // Nacelle dimensions
    float nacelleWidth = 0.18f;
    float nacelleLength = 0.45f;

    // Central spine
    float spineWidth = 0.22f;
    float spineHeight = 0.04f;

    MeshBuilder build() const override {
        // Build long, narrow arrowhead shape
        // The craft is like a sleek needle - MUCH longer than wide

        // === MAIN BODY ===
        // Long tapered fuselage
        auto body = MeshBuilder::box(length * 0.5f, height, spineWidth * 1.4f)
            .translate({-length * 0.1f, 0, 0});

        // Forward fuselage taper - long wedge pointing forward
        auto forwardTaper = MeshBuilder::wedge(length * 0.45f, height, spineWidth * 1.3f)
            .rotate(glm::radians(180.0f), {0, 1, 0})  // Point forward
            .translate({length * 0.12f, 0, 0});
        body.append(forwardTaper);

        // === SWEPT WINGS ===
        // Narrow wings that sweep back along the body
        float wingChord = length * 0.35f;  // Wing length (front to back)
        float wingSpan = width * 0.38f;    // Half-wingspan (narrow)

        for (float zSign : {1.0f, -1.0f}) {
            // Main wing - triangular, swept back
            auto wing = MeshBuilder::wedge(wingChord, wingSpan, height * 0.9f)
                .rotate(glm::radians(-90.0f * zSign), {1, 0, 0})  // Lay flat
                .rotate(glm::radians(180.0f), {0, 1, 0})  // Point taper backward
                .translate({-length * 0.22f, 0, zSign * wingSpan * 0.5f});
            body.append(wing);

            // Wing root filler
            auto wingRoot = MeshBuilder::box(wingChord * 0.7f, height * 0.95f, wingSpan * 0.4f)
                .translate({-length * 0.18f, 0, zSign * wingSpan * 0.22f});
            body.append(wingRoot);
        }

        // === REAR SECTION ===
        // Tapered rear
        auto rearTaper = MeshBuilder::wedge(length * 0.18f, height * 0.9f, spineWidth * 1.2f)
            .translate({-length * 0.42f, 0, 0});
        body.append(rearTaper);

        // === CENTRAL SPINE RIDGE ===
        auto spine = MeshBuilder::box(length * 0.45f, spineHeight, spineWidth * 0.7f)
            .translate({-length * 0.05f, height * 0.5f + spineHeight * 0.5f, 0});
        body.append(spine);

        // === ENGINE NACELLES ===
        // On the rear wing area
        for (float zSign : {1.0f, -1.0f}) {
            float zPos = zSign * (width * 0.32f);

            // Nacelle main body
            auto nacelle = MeshBuilder::box(nacelleLength, height * 1.3f, nacelleWidth)
                .translate({-length * 0.30f, height * 0.6f, zPos});
            body.append(nacelle);

            // Nacelle front taper
            auto nacelleFront = MeshBuilder::wedge(0.15f, height * 1.1f, nacelleWidth * 0.85f)
                .rotate(glm::radians(180.0f), {0, 1, 0})
                .translate({-length * 0.30f + nacelleLength * 0.5f + 0.06f, height * 0.55f, zPos});
            body.append(nacelleFront);

            // Nacelle rear exhaust housing
            auto exhaustHousing = MeshBuilder::box(0.06f, height * 1.5f, nacelleWidth * 1.0f)
                .translate({-length * 0.30f - nacelleLength * 0.5f - 0.01f, height * 0.65f, zPos});
            body.append(exhaustHousing);
        }

        // === VERTICAL STABILIZERS ===
        // Small fins at wing tips
        for (float zSign : {1.0f, -1.0f}) {
            float zPos = zSign * (width * 0.42f);
            auto fin = MeshBuilder::box(0.10f, height * 1.6f, 0.012f)
                .translate({-length * 0.35f, height * 0.8f, zPos});
            body.append(fin);
        }

        // === PANEL LINES ===
        float panelDepth = 0.004f;
        float panelWidth = 0.005f;

        for (float zOffset : {0.07f, -0.07f}) {
            auto line = MeshBuilder::box(length * 0.3f, panelDepth, panelWidth)
                .translate({0.0f, height * 0.5f + spineHeight + panelDepth * 0.3f, zOffset});
            body.subtract(line);
        }

        return body;
    }
};

// =============================================================================
// NoseNeedle - Long pointed front extension
// =============================================================================

class NoseNeedle : public CraftPart {
public:
    float length = 0.7f;        // Long needle
    float baseWidth = 0.12f;    // Width at base
    float baseHeight = 0.08f;   // Height at base
    int sides = 4;              // Angular (square cross-section)

    NoseNeedle() {
        m_offset = {1.35f, 0.0f, 0};  // At front of long body
    }

    MeshBuilder build() const override {
        // Main needle - pyramid pointing forward
        auto needle = MeshBuilder::pyramid(baseWidth, length, sides)
            .rotate(glm::radians(-90.0f), {0, 0, 1})  // Point forward (+X)
            .scale({1.0f, baseHeight / baseWidth, 1.0f})  // Flatten vertically
            .translate(m_offset);

        // Transition piece at base
        auto transition = MeshBuilder::frustum(baseWidth * 0.7f, baseWidth * 0.5f, 0.08f, sides)
            .rotate(glm::radians(-90.0f), {0, 0, 1})
            .scale({1.0f, baseHeight / baseWidth * 1.2f, 1.0f})
            .translate({m_offset.x - 0.02f, m_offset.y, m_offset.z});
        needle.append(transition);

        // Sensor array at very tip (small sphere)
        auto sensor = MeshBuilder::sphere(0.012f, 4)
            .translate({m_offset.x + length * 0.48f, m_offset.y, m_offset.z});
        needle.append(sensor);

        return needle;
    }
};

// =============================================================================
// LowCockpit - Recessed angular canopy
// =============================================================================

class LowCockpit : public CraftPart {
public:
    float length = 0.28f;
    float width = 0.16f;
    float height = 0.05f;       // Very low profile

    float windscreenLength = 0.12f;
    float windscreenAngle = 25.0f;  // Slope angle

    LowCockpit() {
        m_offset = {0.35f, 0.10f, 0};  // On top of spine, toward front of long body
    }

    MeshBuilder build() const override {
        // Main canopy - low box
        auto cockpit = MeshBuilder::box(length, height, width)
            .translate(m_offset);

        // Sloped windscreen (wedge angled backward)
        auto windscreen = MeshBuilder::wedge(windscreenLength, height * 1.2f, width * 0.9f)
            .rotate(glm::radians(180.0f), {0, 1, 0})  // Point forward
            .translate({m_offset.x + length * 0.5f + windscreenLength * 0.3f,
                       m_offset.y + height * 0.1f, m_offset.z});
        cockpit.append(windscreen);

        // Canopy frame rails
        for (float zSign : {1.0f, -1.0f}) {
            auto rail = MeshBuilder::box(length * 0.7f, 0.012f, 0.012f)
                .translate({m_offset.x, m_offset.y + height * 0.5f + 0.006f,
                           zSign * width * 0.42f});
            cockpit.append(rail);
        }

        // Rear headrest bump
        auto headrest = MeshBuilder::box(0.05f, 0.025f, width * 0.5f)
            .translate({m_offset.x - length * 0.4f, m_offset.y + height * 0.5f + 0.012f, 0});
        cockpit.append(headrest);

        return cockpit;
    }
};

// =============================================================================
// EngineExhaust - Emissive glow geometry at rear of nacelles
// =============================================================================

class EngineExhaust : public CraftPart {
public:
    float innerWidth = 0.10f;
    float innerHeight = 0.07f;
    float outerWidth = 0.14f;
    float outerHeight = 0.10f;
    float length = 0.20f;
    int segments = 6;

    bool isRight = false;

    EngineExhaust(bool right = false) : isRight(right) {
        // Match nacelle positions: width * 0.32 = 0.9 * 0.32 = 0.29
        float zOffset = right ? -0.29f : 0.29f;
        m_offset = {-0.95f, 0.08f, zOffset};  // Behind nacelles at rear
    }

    MeshBuilder build() const override {
        // Inner bright core - cone pointing backward
        auto glow = MeshBuilder::frustum(innerWidth * 0.5f, innerWidth * 0.15f, length, segments)
            .rotate(glm::radians(-90.0f), {0, 0, 1})  // Point backward (-X)
            .scale({1.0f, innerHeight / innerWidth, 1.0f})  // Flatten to oval
            .translate(m_offset);

        // Outer glow cone
        auto outer = MeshBuilder::frustum(outerWidth * 0.5f, outerWidth * 0.1f, length * 1.3f, segments)
            .rotate(glm::radians(-90.0f), {0, 0, 1})
            .scale({1.0f, outerHeight / outerWidth, 1.0f})
            .translate({m_offset.x - 0.04f, m_offset.y, m_offset.z});
        glow.append(outer);

        return glow;
    }
};

// =============================================================================
// VerticalFin - Small rear stabilizer (optional, more subtle than before)
// =============================================================================

class VerticalFin : public CraftPart {
public:
    float baseLength = 0.14f;
    float height = 0.16f;
    float thickness = 0.012f;
    float sweepAngle = 40.0f;

    VerticalFin() {
        m_offset = {-0.90f, 0.12f, 0};  // At rear of long body
    }

    MeshBuilder build() const override {
        // Swept vertical fin - use wedge rotated up
        auto fin = MeshBuilder::wedge(baseLength, height, thickness)
            .rotate(glm::radians(90.0f), {0, 0, 1})   // Stand up
            .rotate(glm::radians(-sweepAngle), {0, 0, 1})  // Sweep back
            .translate(m_offset);

        return fin;
    }
};

// =============================================================================
// HoverPad - Anti-gravity emitter on underside (simplified)
// =============================================================================

class HoverPad : public CraftPart {
public:
    float radius = 0.035f;
    float depth = 0.012f;
    int segments = 6;

    bool isRight = false;
    bool isFront = true;

    HoverPad(bool right = false, bool front = true) : isRight(right), isFront(front) {
        // Position on underside - narrower spread for arrow shape
        float zOffset = right ? -0.28f : 0.28f;
        float xOffset = front ? 0.50f : -0.70f;
        m_offset = {xOffset, -0.06f, zOffset};
    }

    MeshBuilder build() const override {
        auto pad = MeshBuilder::cylinder(radius, depth, segments)
            .translate(m_offset);

        // Inner emitter
        auto emitter = MeshBuilder::cylinder(radius * 0.5f, depth * 0.6f, segments)
            .translate({m_offset.x, m_offset.y + depth * 0.2f, m_offset.z});
        pad.append(emitter);

        return pad;
    }
};

// =============================================================================
// Craft - Complete anti-gravity racing craft (delta-wing design)
// =============================================================================

class Craft {
public:
    // Main components
    DeltaBody body;
    NoseNeedle nose;
    LowCockpit cockpit;
    VerticalFin fin;

    // Hover pads (4 corners)
    HoverPad hoverFrontLeft{false, true};
    HoverPad hoverFrontRight{true, true};
    HoverPad hoverRearLeft{false, false};
    HoverPad hoverRearRight{true, false};

    // Engine exhaust (separate for emissive material)
    EngineExhaust leftExhaust{false};
    EngineExhaust rightExhaust{true};

    /// Build the complete craft mesh (body only, no glow)
    MeshBuilder build() const {
        auto mesh = body.build();

        mesh.append(nose.build());
        mesh.append(cockpit.build());
        mesh.append(fin.build());

        // Hover pads
        mesh.append(hoverFrontLeft.build());
        mesh.append(hoverFrontRight.build());
        mesh.append(hoverRearLeft.build());
        mesh.append(hoverRearRight.build());

        // Apply flat normals for faceted PS1 look
        mesh.computeFlatNormals();

        // Project UVs from top-down for livery mapping
        mesh.projectUVsNormalized(Axis::Y);

        return mesh;
    }

    /// Build the engine glow mesh (for emissive material)
    MeshBuilder buildEngineGlow() const {
        auto mesh = leftExhaust.build();
        mesh.append(rightExhaust.build());
        mesh.computeFlatNormals();
        return mesh;
    }
};
