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
// UnifiedBody - Based on alexandre-etendard reference images
// Built from CUSTOM VERTICES for a true unified arrowhead shape
// Long nose (~40%), widening body, W-shaped trailing edge, corner nacelles
// =============================================================================

class DeltaBody : public CraftPart {
public:
    // Overall dimensions - VERY elongated like reference
    float length = 3.2f;        // Total nose to tail (longer!)
    float maxWidth = 1.0f;      // Width at rear (narrower for needle look)
    float height = 0.065f;      // Very flat, blade-like

    // Surface detail dimensions
    float nacelleHeight = 0.12f;
    float spineHeight = 0.030f;

    MeshBuilder build() const override {
        // =================================================================
        // UNIFIED ARROWHEAD - Delta wing using wedges laid flat
        // Long needle nose tapering to wide rear with anhedral
        // =================================================================

        // Key dimensions
        float noseLen = length * 0.45f;      // Long nose (~45% of craft)
        float bodyLen = length * 0.55f;      // Main body section
        float rearWidth = maxWidth * 0.50f;  // Half-width at rear
        float anhedralAngle = 10.0f;         // Degrees - wings fold down

        MeshBuilder body;

        // --- NOSE: Long tapered wedge pointing forward ---
        auto nose = MeshBuilder::wedge(noseLen, height, maxWidth * 0.18f)
            .rotate(glm::radians(180.0f), {0, 1, 0})  // Point forward
            .translate({noseLen * 0.5f, 0, 0});
        body.append(nose);

        // --- DELTA WINGS: Two wedges laid flat, creating arrowhead planform ---
        // Each wing wedge: length along X, width (laid flat) becomes Z span
        // Wedge tapers from wide at rear to narrow at front

        float wingLength = bodyLen * 1.0f;   // Full body length
        float wingSpan = rearWidth * 0.95f;  // How far wing extends from center

        // Left wing - wedge laid flat, tilted down for anhedral
        auto leftWing = MeshBuilder::wedge(wingLength, wingSpan, height)
            .rotate(glm::radians(-90.0f), {1, 0, 0})   // Lay flat (Z becomes width)
            .rotate(glm::radians(anhedralAngle), {1, 0, 0})  // Anhedral tilt
            .translate({-bodyLen * 0.35f,
                       -wingSpan * 0.4f * sin(glm::radians(anhedralAngle)),
                       wingSpan * 0.5f * cos(glm::radians(anhedralAngle))});
        body.append(leftWing);

        // Right wing - mirror of left
        auto rightWing = MeshBuilder::wedge(wingLength, wingSpan, height)
            .rotate(glm::radians(90.0f), {1, 0, 0})    // Lay flat (opposite direction)
            .rotate(glm::radians(-anhedralAngle), {1, 0, 0})  // Anhedral tilt
            .translate({-bodyLen * 0.35f,
                       -wingSpan * 0.4f * sin(glm::radians(anhedralAngle)),
                       -wingSpan * 0.5f * cos(glm::radians(anhedralAngle))});
        body.append(rightWing);

        // --- CENTRAL SPINE: Thin ridge connecting nose to rear ---
        auto spine = MeshBuilder::box(bodyLen * 0.85f, height * 1.2f, maxWidth * 0.12f)
            .translate({-bodyLen * 0.30f, 0, 0});
        body.append(spine);

        // --- REAR NACELLES: Engine housings at wing tips ---
        float nacelleDropY = -wingSpan * sin(glm::radians(anhedralAngle)) * 0.8f;

        for (float zSign : {1.0f, -1.0f}) {
            float zPos = zSign * rearWidth * 0.80f;
            float yPos = nacelleDropY * zSign;

            // Nacelle body
            auto nacelle = MeshBuilder::box(bodyLen * 0.30f, nacelleHeight, maxWidth * 0.12f)
                .rotate(glm::radians(anhedralAngle * zSign), {1, 0, 0})
                .translate({-bodyLen * 0.50f, yPos + nacelleHeight * 0.4f, zPos});
            body.append(nacelle);

            // Nacelle front taper
            auto nacelleFront = MeshBuilder::wedge(bodyLen * 0.12f, nacelleHeight * 0.85f, maxWidth * 0.11f)
                .rotate(glm::radians(180.0f), {0, 1, 0})
                .rotate(glm::radians(anhedralAngle * zSign), {1, 0, 0})
                .translate({-bodyLen * 0.28f, yPos + nacelleHeight * 0.38f, zPos});
            body.append(nacelleFront);
        }

        // =================================================================
        // SURFACE DETAILS
        // =================================================================

        float yTop = height * 0.5f;

        // --- Spine Ridge on top ---
        auto spineRidge = MeshBuilder::box(length * 0.30f, spineHeight, 0.08f)
            .translate({0.0f, yTop + spineHeight * 0.5f, 0});
        body.append(spineRidge);

        auto spineTaper = MeshBuilder::wedge(noseLen * 0.30f, spineHeight * 0.8f, 0.07f)
            .rotate(glm::radians(180.0f), {0, 1, 0})
            .translate({noseLen * 0.20f, yTop + spineHeight * 0.4f, 0});
        body.append(spineTaper);

        // --- Tubes and Pipes ---
        float tubeR = 0.010f;
        int tubeSeg = 6;

        for (float zSign : {1.0f, -1.0f}) {
            float nacelleZ = zSign * rearWidth * 0.80f;
            float nacelleY = nacelleDropY * zSign;

            // Fuel line along spine
            auto fuelLine = MeshBuilder::cylinder(tubeR, length * 0.18f, tubeSeg)
                .rotate(glm::radians(90.0f), {0, 0, 1})
                .translate({-bodyLen * 0.05f, yTop + 0.015f, zSign * 0.06f});
            body.append(fuelLine);

            // Pipe on nacelle
            auto nacellePipe = MeshBuilder::cylinder(tubeR * 0.7f, bodyLen * 0.10f, tubeSeg)
                .rotate(glm::radians(90.0f), {0, 0, 1})
                .translate({-bodyLen * 0.42f, nacelleY + nacelleHeight * 0.9f, nacelleZ});
            body.append(nacellePipe);
        }

        // --- Vertical Fins at Nacelle Rear ---
        for (float zSign : {1.0f, -1.0f}) {
            float nacelleZ = zSign * rearWidth * 0.80f;
            float nacelleY = nacelleDropY * zSign;
            auto fin = MeshBuilder::box(bodyLen * 0.05f, nacelleHeight * 0.6f, 0.006f)
                .translate({-bodyLen * 0.56f, nacelleY + nacelleHeight * 0.7f, nacelleZ});
            body.append(fin);
        }

        return body;
    }
};

// =============================================================================
// NoseNeedle - Long pointed front extension
// =============================================================================

class NoseNeedle : public CraftPart {
public:
    // Nose extension - extends the unified body's point further
    float length = 0.25f;
    float baseWidth = 0.04f;
    float baseHeight = 0.025f;
    int sides = 4;

    NoseNeedle() {
        // Body nose tip is at noseLen * 0.5 = 3.2 * 0.45 * 0.5 = 0.72
        m_offset = {0.72f, 0.0f, 0};
    }

    MeshBuilder build() const override {
        // Extended nose needle
        auto needle = MeshBuilder::pyramid(baseWidth, length, sides)
            .rotate(glm::radians(-90.0f), {0, 0, 1})
            .scale({1.0f, baseHeight / baseWidth, 1.0f})
            .translate(m_offset);

        // Small sensor at tip
        auto sensor = MeshBuilder::sphere(0.006f, 4)
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
    float length = 0.22f;
    float width = 0.12f;
    float height = 0.038f;      // Very low profile

    float windscreenLength = 0.09f;
    float windscreenAngle = 25.0f;

    LowCockpit() {
        // Cockpit on central fuselage, yTop = height/2 = 0.0325
        m_offset = {0.0f, 0.05f, 0};
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
    float innerWidth = 0.08f;
    float innerHeight = 0.06f;
    float outerWidth = 0.12f;
    float outerHeight = 0.09f;
    float length = 0.18f;
    int segments = 6;

    bool isRight = false;

    EngineExhaust(bool right = false) : isRight(right) {
        // Match nacelle positions from new geometry
        // bodyLen = 1.76, rearWidth = 0.55, anhedralAngle = 12°
        float zOffset = right ? -0.47f : 0.47f;
        float anhedralDrop = 0.55f * sin(glm::radians(12.0f)) * 0.85f;  // ~0.10
        m_offset = {-0.95f, 0.02f - anhedralDrop, zOffset};
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
    float baseLength = 0.12f;
    float height = 0.14f;
    float thickness = 0.010f;
    float sweepAngle = 40.0f;

    VerticalFin() {
        // Rear center of fuselage
        m_offset = {-0.75f, 0.05f, 0};
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
    float radius = 0.028f;
    float depth = 0.010f;
    int segments = 6;

    bool isRight = false;
    bool isFront = true;

    HoverPad(bool right = false, bool front = true) : isRight(right), isFront(front) {
        // Position on underside of unified body
        float zOffset = right ? (front ? -0.08f : -0.35f) : (front ? 0.08f : 0.35f);
        float xOffset = front ? 0.25f : -0.80f;
        // Rear pads follow anhedral
        float anhedralDrop = front ? 0.0f : 0.55f * sin(glm::radians(12.0f)) * 0.7f;
        m_offset = {xOffset, -0.033f - anhedralDrop, zOffset};
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
