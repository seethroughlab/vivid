#pragma once

/**
 * @file sweep.h
 * @brief Sweep operator - extrude 2D profile along 3D path
 *
 * Creates meshes by sweeping a cross-section profile along a parametric path.
 * Self-contained with built-in path types (Line, Helix, Circle, Arc) and
 * profile types (Circle, Square, Star, Triangle).
 *
 * @par Example
 * @code
 * auto& helix = chain.add<Sweep>("helix");
 * helix.pathType(SweepPath::Helix);
 * helix.pathRadius(1.0f);
 * helix.pathHeight(4.0f);
 * helix.pathTurns(3.0f);
 * helix.profileRadius(0.15f);
 * helix.twist(glm::two_pi<float>());
 * @endcode
 */

#include <vivid/render3d/geometry_operator.h>
#include <vivid/render3d/mesh_builder.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>
#include <vivid/context.h>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <vector>

namespace vivid::render3d {

/// Path types for sweep operation
enum class SweepPath {
    Line,    ///< Straight line along Y axis
    Helix,   ///< Helical path (spiral)
    Circle,  ///< Closed circular path in XZ plane
    Arc      ///< Partial circular arc
};

/// Profile shapes for sweep cross-section
enum class SweepProfile {
    Circle,   ///< Circular cross-section
    Square,   ///< Square cross-section
    Star,     ///< 5-pointed star
    Triangle  ///< Triangular cross-section
};

/**
 * @brief Sweep operator - extrude profile along path
 *
 * Creates geometry by sweeping a 2D cross-section along a 3D path.
 * Supports various path types and profile shapes with modifiers
 * for twist, scale variation, and end caps.
 *
 * @par Example: Helix with twist
 * @code
 * auto& helix = chain.add<Sweep>("helix");
 * helix.pathType(SweepPath::Helix);
 * helix.pathRadius(1.0f);
 * helix.pathHeight(4.0f);
 * helix.pathTurns(3.0f);
 * helix.profileRadius(0.15f);
 * helix.twist(glm::two_pi<float>());
 * @endcode
 *
 * @par Example: Tapered tube
 * @code
 * auto& tube = chain.add<Sweep>("tube");
 * tube.pathType(SweepPath::Line);
 * tube.pathHeight(2.0f);
 * tube.profileRadius(0.4f);
 * tube.scaleStart(1.0f);
 * tube.scaleEnd(0.2f);
 * @endcode
 */
class Sweep : public GeometryOperator {
public:
    Sweep() {
        // Path params
        registerParam(m_pathHeight);
        registerParam(m_pathRadius);
        registerParam(m_pathTurns);
        registerParam(m_pathSegments);
        registerParam(m_arcAngle);

        // Profile params
        registerParam(m_profileRadius);
        registerParam(m_profileSegments);

        // Modifiers
        registerParam(m_twist);
        registerParam(m_scaleStart);
        registerParam(m_scaleEnd);
        registerParam(m_caps);
    }

    // -------------------------------------------------------------------------
    /// @name Path Configuration
    /// @{

    /// Set path type (Line, Helix, Circle, Arc)
    void pathType(SweepPath type) {
        if (m_pathType != type) {
            m_pathType = type;
            markDirty();
        }
    }

    /// Set path height (for Line and Helix)
    void pathHeight(float h) {
        if (static_cast<float>(m_pathHeight) != h) {
            m_pathHeight = h;
            markDirty();
        }
    }

    /// Set path radius (for Helix, Circle, Arc)
    void pathRadius(float r) {
        if (static_cast<float>(m_pathRadius) != r) {
            m_pathRadius = r;
            markDirty();
        }
    }

    /// Set number of turns (for Helix)
    void pathTurns(float turns) {
        if (static_cast<float>(m_pathTurns) != turns) {
            m_pathTurns = turns;
            markDirty();
        }
    }

    /// Set number of path segments
    void pathSegments(int s) {
        if (static_cast<int>(m_pathSegments) != s) {
            m_pathSegments = s;
            markDirty();
        }
    }

    /// Set arc angle in radians (for Arc path type)
    void arcAngle(float radians) {
        if (static_cast<float>(m_arcAngle) != radians) {
            m_arcAngle = radians;
            markDirty();
        }
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Profile Configuration
    /// @{

    /// Set profile type (Circle, Square, Star, Triangle)
    void profileType(SweepProfile type) {
        if (m_profileType != type) {
            m_profileType = type;
            markDirty();
        }
    }

    /// Set profile radius
    void profileRadius(float r) {
        if (static_cast<float>(m_profileRadius) != r) {
            m_profileRadius = r;
            markDirty();
        }
    }

    /// Set number of profile segments (for circular profile)
    void profileSegments(int s) {
        if (static_cast<int>(m_profileSegments) != s) {
            m_profileSegments = s;
            markDirty();
        }
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Modifiers
    /// @{

    /// Set twist amount in radians (profile rotation along path)
    void twist(float radians) {
        if (static_cast<float>(m_twist) != radians) {
            m_twist = radians;
            markDirty();
        }
    }

    /// Set scale at path start
    void scaleStart(float s) {
        if (static_cast<float>(m_scaleStart) != s) {
            m_scaleStart = s;
            markDirty();
        }
    }

    /// Set scale at path end
    void scaleEnd(float s) {
        if (static_cast<float>(m_scaleEnd) != s) {
            m_scaleEnd = s;
            markDirty();
        }
    }

    /// Enable/disable end caps (for open paths)
    void caps(bool enabled) {
        if (static_cast<bool>(m_caps) != enabled) {
            m_caps = enabled;
            markDirty();
        }
    }

    /// @}

    void process(Context& ctx) override {
        if (needsCook()) {
            generateMesh();
            finalizeMesh(ctx);
        }
        updatePreview(ctx);
    }

    std::string name() const override { return "Sweep"; }

private:
    // Path type (stored separately, not a Param)
    SweepPath m_pathType = SweepPath::Line;
    SweepProfile m_profileType = SweepProfile::Circle;

    // Path params
    Param<float> m_pathHeight{"pathHeight", 2.0f, 0.01f, 100.0f};
    Param<float> m_pathRadius{"pathRadius", 1.0f, 0.01f, 100.0f};
    Param<float> m_pathTurns{"pathTurns", 1.0f, 0.0f, 20.0f};
    Param<int> m_pathSegments{"pathSegments", 32, 3, 256};
    Param<float> m_arcAngle{"arcAngle", glm::pi<float>(), 0.01f, glm::two_pi<float>()};

    // Profile params
    Param<float> m_profileRadius{"profileRadius", 0.2f, 0.001f, 50.0f};
    Param<int> m_profileSegments{"profileSegments", 16, 3, 64};

    // Modifiers
    Param<float> m_twist{"twist", 0.0f, -12.566f, 12.566f};
    Param<float> m_scaleStart{"scaleStart", 1.0f, 0.01f, 5.0f};
    Param<float> m_scaleEnd{"scaleEnd", 1.0f, 0.01f, 5.0f};
    Param<bool> m_caps{"caps", true, false, true};

    // -------------------------------------------------------------------------
    /// @name Internal Methods
    /// @{

    /// Generate the sweep mesh
    void generateMesh() {
        m_builder.clear();

        int pathSegs = static_cast<int>(m_pathSegments);
        int profSegs = getProfileSegmentCount();

        // Generate profile points (in XY plane, centered at origin)
        std::vector<glm::vec2> profile = generateProfile();

        // Check if path is closed (Circle path type)
        bool pathClosed = (m_pathType == SweepPath::Circle);

        // Generate vertices by sweeping profile along path
        for (int i = 0; i <= pathSegs; i++) {
            float t = static_cast<float>(i) / static_cast<float>(pathSegs);

            // Skip last ring for closed paths (will wrap to first)
            if (pathClosed && i == pathSegs) continue;

            // Get path position and frame
            glm::vec3 pos = evaluatePath(t);
            glm::vec3 T, N, B;
            computeFrame(t, T, N, B);

            // Compute scale and twist for this position
            float scale = glm::mix(static_cast<float>(m_scaleStart),
                                   static_cast<float>(m_scaleEnd), t);
            float twistAngle = t * static_cast<float>(m_twist);

            // Generate profile vertices at this position
            for (int j = 0; j < profSegs; j++) {
                glm::vec2 p = profile[j] * scale;

                // Apply twist rotation in the N-B plane
                float cosT = std::cos(twistAngle);
                float sinT = std::sin(twistAngle);
                glm::vec2 twisted(p.x * cosT - p.y * sinT,
                                  p.x * sinT + p.y * cosT);

                // Transform to world space using frame
                glm::vec3 worldPos = pos + twisted.x * N + twisted.y * B;

                // Compute normal (pointing outward from profile center)
                glm::vec3 localNormal = glm::normalize(glm::vec3(twisted.x, twisted.y, 0.0f));
                glm::vec3 worldNormal = localNormal.x * N + localNormal.y * B;

                // UV mapping: U around profile, V along path
                float u = static_cast<float>(j) / static_cast<float>(profSegs);
                float v = t;

                m_builder.addVertex(worldPos, worldNormal, glm::vec2(u, v));
            }
        }

        // Generate faces connecting adjacent rings
        int ringCount = pathClosed ? pathSegs : pathSegs + 1;
        for (int i = 0; i < (pathClosed ? pathSegs : pathSegs); i++) {
            int nextRing = (i + 1) % ringCount;

            for (int j = 0; j < profSegs; j++) {
                int nextProf = (j + 1) % profSegs;

                uint32_t a = i * profSegs + j;
                uint32_t b = i * profSegs + nextProf;
                uint32_t c = nextRing * profSegs + nextProf;
                uint32_t d = nextRing * profSegs + j;

                m_builder.addQuad(a, b, c, d);
            }
        }

        // Add caps for open paths
        if (!pathClosed && static_cast<bool>(m_caps)) {
            addCap(true);   // Start cap
            addCap(false);  // End cap
        }
    }

    /// Evaluate path position at parameter t (0 to 1)
    glm::vec3 evaluatePath(float t) const {
        switch (m_pathType) {
            case SweepPath::Line: {
                float h = static_cast<float>(m_pathHeight);
                return glm::vec3(0.0f, t * h - h * 0.5f, 0.0f);
            }

            case SweepPath::Helix: {
                float h = static_cast<float>(m_pathHeight);
                float r = static_cast<float>(m_pathRadius);
                float turns = static_cast<float>(m_pathTurns);
                float angle = t * turns * glm::two_pi<float>();
                return glm::vec3(
                    r * std::cos(angle),
                    t * h - h * 0.5f,
                    r * std::sin(angle)
                );
            }

            case SweepPath::Circle: {
                float r = static_cast<float>(m_pathRadius);
                float angle = t * glm::two_pi<float>();
                return glm::vec3(
                    r * std::cos(angle),
                    0.0f,
                    r * std::sin(angle)
                );
            }

            case SweepPath::Arc: {
                float r = static_cast<float>(m_pathRadius);
                float arcAng = static_cast<float>(m_arcAngle);
                float angle = t * arcAng - arcAng * 0.5f;
                return glm::vec3(
                    r * std::cos(angle),
                    0.0f,
                    r * std::sin(angle)
                );
            }
        }
        return glm::vec3(0.0f);
    }

    /// Compute Frenet frame (Tangent, Normal, Binormal) at parameter t
    void computeFrame(float t, glm::vec3& T, glm::vec3& N, glm::vec3& B) const {
        const float epsilon = 0.0001f;

        // Compute tangent using finite differences
        float t0 = std::max(0.0f, t - epsilon);
        float t1 = std::min(1.0f, t + epsilon);
        glm::vec3 p0 = evaluatePath(t0);
        glm::vec3 p1 = evaluatePath(t1);
        T = glm::normalize(p1 - p0);

        // Handle degenerate case (straight line)
        if (m_pathType == SweepPath::Line) {
            // Use fixed frame for straight line
            T = glm::vec3(0.0f, 1.0f, 0.0f);
            N = glm::vec3(1.0f, 0.0f, 0.0f);
            B = glm::vec3(0.0f, 0.0f, 1.0f);
            return;
        }

        // For curved paths, compute normal from path curvature
        // Use a reference vector that's not parallel to T
        glm::vec3 ref = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(T, ref)) > 0.99f) {
            ref = glm::vec3(1.0f, 0.0f, 0.0f);
        }

        // Compute binormal and normal
        B = glm::normalize(glm::cross(T, ref));
        N = glm::normalize(glm::cross(B, T));
    }

    /// Generate 2D profile points
    std::vector<glm::vec2> generateProfile() const {
        std::vector<glm::vec2> points;
        float r = static_cast<float>(m_profileRadius);
        int segs = getProfileSegmentCount();

        switch (m_profileType) {
            case SweepProfile::Circle: {
                for (int i = 0; i < segs; i++) {
                    float angle = static_cast<float>(i) / static_cast<float>(segs) * glm::two_pi<float>();
                    points.push_back(glm::vec2(r * std::cos(angle), r * std::sin(angle)));
                }
                break;
            }

            case SweepProfile::Square: {
                // Square with 4 corners (segs should ideally be multiple of 4)
                points.push_back(glm::vec2(-r, -r));
                points.push_back(glm::vec2(r, -r));
                points.push_back(glm::vec2(r, r));
                points.push_back(glm::vec2(-r, r));
                break;
            }

            case SweepProfile::Star: {
                // 5-pointed star
                int starPoints = 5;
                float innerR = r * 0.4f;
                for (int i = 0; i < starPoints * 2; i++) {
                    float angle = static_cast<float>(i) / static_cast<float>(starPoints * 2) * glm::two_pi<float>()
                                  - glm::half_pi<float>();  // Start at top
                    float rad = (i % 2 == 0) ? r : innerR;
                    points.push_back(glm::vec2(rad * std::cos(angle), rad * std::sin(angle)));
                }
                break;
            }

            case SweepProfile::Triangle: {
                // Equilateral triangle
                for (int i = 0; i < 3; i++) {
                    float angle = static_cast<float>(i) / 3.0f * glm::two_pi<float>() - glm::half_pi<float>();
                    points.push_back(glm::vec2(r * std::cos(angle), r * std::sin(angle)));
                }
                break;
            }
        }

        return points;
    }

    /// Get actual profile segment count based on profile type
    int getProfileSegmentCount() const {
        switch (m_profileType) {
            case SweepProfile::Circle:
                return static_cast<int>(m_profileSegments);
            case SweepProfile::Square:
                return 4;
            case SweepProfile::Star:
                return 10;  // 5 points * 2
            case SweepProfile::Triangle:
                return 3;
        }
        return static_cast<int>(m_profileSegments);
    }

    /// Add end cap (fan triangulation)
    void addCap(bool isStart) {
        std::vector<glm::vec2> profile = generateProfile();
        int profSegs = getProfileSegmentCount();
        int pathSegs = static_cast<int>(m_pathSegments);

        float t = isStart ? 0.0f : 1.0f;
        glm::vec3 pos = evaluatePath(t);
        glm::vec3 T, N, B;
        computeFrame(t, T, N, B);

        float scale = isStart ? static_cast<float>(m_scaleStart)
                              : static_cast<float>(m_scaleEnd);
        float twistAngle = t * static_cast<float>(m_twist);

        // Cap normal (opposite to tangent direction for start, same for end)
        glm::vec3 capNormal = isStart ? -T : T;

        // Add center vertex
        uint32_t centerIdx = static_cast<uint32_t>(m_builder.vertexCount());
        m_builder.addVertex(pos, capNormal, glm::vec2(0.5f, 0.5f));

        // Add edge vertices
        std::vector<uint32_t> edgeIndices;
        for (int j = 0; j < profSegs; j++) {
            glm::vec2 p = profile[j] * scale;

            float cosT = std::cos(twistAngle);
            float sinT = std::sin(twistAngle);
            glm::vec2 twisted(p.x * cosT - p.y * sinT,
                              p.x * sinT + p.y * cosT);

            glm::vec3 worldPos = pos + twisted.x * N + twisted.y * B;

            // UV for cap (normalized position)
            glm::vec2 uv(0.5f + twisted.x / (static_cast<float>(m_profileRadius) * 2.0f),
                         0.5f + twisted.y / (static_cast<float>(m_profileRadius) * 2.0f));

            edgeIndices.push_back(static_cast<uint32_t>(m_builder.vertexCount()));
            m_builder.addVertex(worldPos, capNormal, uv);
        }

        // Create triangles (fan from center)
        for (int j = 0; j < profSegs; j++) {
            int nextJ = (j + 1) % profSegs;
            if (isStart) {
                // Reverse winding for start cap
                m_builder.addTriangle(centerIdx, edgeIndices[nextJ], edgeIndices[j]);
            } else {
                m_builder.addTriangle(centerIdx, edgeIndices[j], edgeIndices[nextJ]);
            }
        }
    }

    /// @}
};

} // namespace vivid::render3d
