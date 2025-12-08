#pragma once

/**
 * @file boolean.h
 * @brief Boolean CSG operator for combining geometries
 *
 * Provides CSG (Constructive Solid Geometry) operations:
 * - Union: combines two meshes
 * - Subtract: removes one mesh from another
 * - Intersect: keeps only overlapping volume
 */

#include <vivid/render3d/mesh_operator.h>
#include <vivid/render3d/mesh_builder.h>
#include <vivid/context.h>

namespace vivid::render3d {

/**
 * @brief CSG boolean operation types
 */
enum class BooleanOp {
    Union,      ///< Combine meshes (A + B)
    Subtract,   ///< Remove B from A (A - B)
    Intersect   ///< Keep only overlapping volume (A & B)
};

/**
 * @brief Boolean CSG operator
 *
 * Combines two geometry inputs using CSG operations. The result is
 * a new mesh that can be further processed or rendered.
 *
 * @par Example
 * @code
 * auto& box = chain.add<Box>("box").size(1.0f);
 * auto& sphere = chain.add<Sphere>("sphere").radius(0.7f);
 *
 * auto& hollowCube = chain.add<Boolean>("csg")
 *     .inputA(&box)
 *     .inputB(&sphere)
 *     .operation(BooleanOp::Subtract);
 * @endcode
 */
class Boolean : public MeshOperator {
public:
    /// Set the first input (A)
    Boolean& inputA(MeshOperator* op) {
        setInput(0, op);
        return *this;
    }

    /// Set the second input (B)
    Boolean& inputB(MeshOperator* op) {
        setInput(1, op);
        return *this;
    }

    /// Set the boolean operation type
    Boolean& operation(BooleanOp op) {
        m_operation = op;
        return *this;
    }

    /// Enable flat shading on result
    Boolean& flatShading(bool enabled) {
        m_flatShading = enabled;
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        MeshOperator* opA = getMeshInput(0);
        MeshOperator* opB = getMeshInput(1);

        if (!opA || !opB) {
            // No inputs - output empty mesh
            m_mesh = Mesh();
            m_builder.clear();
            return;
        }

        // Get builders from inputs (need manifold data for CSG)
        MeshBuilder* builderA = opA->outputBuilder();
        MeshBuilder* builderB = opB->outputBuilder();

        if (!builderA || !builderB) {
            // Fallback: if no builder available, output empty mesh
            m_mesh = Mesh();
            m_builder.clear();
            return;
        }

        // Copy builder A as our working copy
        m_builder = *builderA;

        // Perform CSG operation
        switch (m_operation) {
            case BooleanOp::Union:
                m_builder.add(*builderB);
                break;
            case BooleanOp::Subtract:
                m_builder.subtract(*builderB);
                break;
            case BooleanOp::Intersect:
                m_builder.intersect(*builderB);
                break;
        }

        // CSG results always use flat normals - the geometry is too complex
        // to automatically determine smooth vs sharp edges
        m_builder.computeFlatNormals();

        m_mesh = m_builder.build();
        m_mesh.upload(ctx);
    }

    void cleanup() override {
        m_mesh.release();
    }

    std::string name() const override { return "Boolean"; }

private:
    BooleanOp m_operation = BooleanOp::Union;
    bool m_flatShading = true;
};

} // namespace vivid::render3d
