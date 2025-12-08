#pragma once

/**
 * @file mesh_operator.h
 * @brief Base class for mesh-producing operators
 *
 * MeshOperators output Mesh data instead of textures. They can be
 * chained together for CSG operations and combined in a SceneComposer
 * before being rendered by Render3D.
 *
 * Important: MeshOperators cannot be chain outputs - only TextureOperators
 * can produce the final output of a chain.
 */

#include <vivid/operator.h>
#include <vivid/render3d/mesh.h>
#include <vivid/render3d/mesh_builder.h>

namespace vivid::render3d {

/**
 * @brief Base class for operators that produce 3D meshes
 *
 * MeshOperator provides a foundation for creating procedural geometry
 * that can be combined through boolean operations and rendered to texture.
 *
 * @par Pipeline Example
 * @code
 * Box -> Boolean(subtract) -> SceneComposer -> Render3D -> Output
 * Sphere -/
 * @endcode
 *
 * @par Example Implementation
 * @code
 * class Box : public MeshOperator {
 * public:
 *     Box& size(float w, float h, float d) {
 *         m_width = w; m_height = h; m_depth = d;
 *         return *this;
 *     }
 *
 *     void process(Context& ctx) override {
 *         auto builder = MeshBuilder::box(m_width, m_height, m_depth);
 *         builder.computeFlatNormals();
 *         m_mesh = builder.build();
 *         m_mesh.upload(ctx);
 *     }
 *
 *     std::string name() const override { return "Box"; }
 *
 * private:
 *     float m_width = 1.0f, m_height = 1.0f, m_depth = 1.0f;
 * };
 * @endcode
 */
class MeshOperator : public Operator {
public:
    virtual ~MeshOperator() = default;

    // -------------------------------------------------------------------------
    /// @name Output Type
    /// @{

    /**
     * @brief Returns Geometry output kind
     * @return OutputKind::Geometry
     *
     * This marks the operator as producing geometry, not textures.
     * Chain validation uses this to prevent mesh operators from
     * being set as chain outputs.
     */
    OutputKind outputKind() const override { return OutputKind::Geometry; }

    /**
     * @brief Returns nullptr (mesh operators don't produce texture views)
     * @return nullptr
     */
    WGPUTextureView outputView() const override { return nullptr; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Mesh Output
    /// @{

    /**
     * @brief Get the output mesh
     * @return Pointer to the output mesh, or nullptr if not yet processed
     *
     * The returned mesh is owned by this operator and remains valid until
     * the next process() call or until the operator is destroyed.
     */
    virtual Mesh* outputMesh() { return &m_mesh; }

    /**
     * @brief Get the output mesh (const version)
     * @return Const pointer to the output mesh
     */
    virtual const Mesh* outputMesh() const { return &m_mesh; }

    /**
     * @brief Get the MeshBuilder (for CSG operations)
     * @return Pointer to the builder with manifold data, or nullptr if not available
     *
     * CSG operations require the manifold representation. Derived classes
     * should store their MeshBuilder and return it here for Boolean operators.
     */
    virtual MeshBuilder* outputBuilder() { return m_builder.vertexCount() > 0 ? &m_builder : nullptr; }

    /**
     * @brief Get the MeshBuilder (const version)
     * @return Const pointer to the builder
     */
    virtual const MeshBuilder* outputBuilder() const { return m_builder.vertexCount() > 0 ? &m_builder : nullptr; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Input Connections
    /// @{

    /**
     * @brief Set a mesh input (fluent interface)
     * @param op MeshOperator to use as input
     * @return Reference to this operator for chaining
     *
     * @par Example
     * @code
     * auto& boolean = chain.add<Boolean>("csg")
     *     .inputA(&box)
     *     .inputB(&sphere);
     * @endcode
     */
    MeshOperator& meshInput(MeshOperator* op) {
        setInput(0, op);
        return *this;
    }

    /**
     * @brief Get mesh input at specified index
     * @param index Input slot index
     * @return MeshOperator pointer, or nullptr if not connected or wrong type
     */
    MeshOperator* getMeshInput(int index = 0) const {
        Operator* op = getInput(index);
        if (op && op->outputKind() == OutputKind::Geometry) {
            return static_cast<MeshOperator*>(op);
        }
        return nullptr;
    }

    /// @}

protected:
    Mesh m_mesh;           ///< Output mesh storage
    MeshBuilder m_builder; ///< Builder with manifold data (for CSG operations)
};

} // namespace vivid::render3d
