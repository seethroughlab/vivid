#pragma once

/**
 * @file static_mesh.h
 * @brief Wrapper that exposes pre-built meshes as MeshOperators
 *
 * StaticMesh allows custom meshes (built via MeshBuilder) to participate
 * in the chain system and appear in the visualizer.
 */

#include <vivid/render3d/mesh_operator.h>

namespace vivid::render3d {

/**
 * @brief Wrapper that exposes a pre-built Mesh as a MeshOperator
 *
 * StaticMesh allows custom meshes (built via MeshBuilder) to participate
 * in the chain system and appear in the visualizer.
 *
 * @par Example
 * @code
 * // Build custom mesh
 * auto builder = MeshBuilder::box(1.0f, 1.0f, 1.0f);
 * builder.computeFlatNormals();
 *
 * // Add to scene via SceneComposer (registered in chain)
 * scene.addMesh("myMesh", builder, transform, color);
 * @endcode
 */
class StaticMesh : public MeshOperator {
public:
    StaticMesh() = default;

    /**
     * @brief Set the mesh (takes ownership via move)
     * @param m Mesh to wrap
     * @return Reference to this operator for chaining
     */
    StaticMesh& mesh(Mesh&& m) {
        m_mesh = std::move(m);
        m_needsUpload = true;
        return *this;
    }

    /**
     * @brief Set the mesh from a MeshBuilder (builds and takes ownership)
     * @param builder MeshBuilder containing the geometry
     * @return Reference to this operator for chaining
     */
    StaticMesh& mesh(MeshBuilder& builder) {
        m_builder = builder;
        m_mesh = m_builder.build();
        m_needsUpload = true;
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        if (m_needsUpload && !m_mesh.vertices.empty()) {
            m_mesh.upload(ctx);
            m_needsUpload = false;
        }
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "StaticMesh"; }

private:
    bool m_needsUpload = false;
};

} // namespace vivid::render3d
