#pragma once

/**
 * @file scene_composer.h
 * @brief Composes multiple geometries into a renderable scene
 *
 * SceneComposer takes geometry inputs and combines them with transforms
 * and colors into a Scene that can be rendered by Render3D.
 */

#include <vivid/render3d/geometry_operator.h>
#include <vivid/render3d/scene.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace vivid::render3d {

/**
 * @brief Entry for a geometry in the composer
 */
struct ComposerEntry {
    GeometryOperator* geometry = nullptr;
    glm::mat4 transform = glm::mat4(1.0f);
    glm::vec4 color = glm::vec4(1.0f);
    int inputIndex = -1;  // Index in inputs_ array, or -1 if not connected
};

/**
 * @brief Combines multiple geometries into a Scene
 *
 * SceneComposer acts as a bridge between GeometryOperators and Render3D.
 * It collects geometry inputs, applies transforms and colors, and
 * produces a Scene for rendering.
 *
 * Note: SceneComposer is a special case - it outputs a Scene rather than
 * a single Mesh. The outputMesh() method returns nullptr.
 *
 * @par Example (Recommended - SceneComposer manages geometry)
 * @code
 * auto& scene = SceneComposer::create(chain, "scene");
 *
 * // Add geometry - automatically registered in chain
 * scene.add<BoxGeometry>("box")
 *     .size(1.0f);
 *
 * scene.add<SphereGeometry>("sphere", glm::translate(glm::mat4(1.0f), glm::vec3(2, 0, 0)))
 *     .radius(0.5f);
 *
 * // For CSG, create inputs via chain (not added to scene)
 * auto& csgBox = chain.add<BoxGeometry>("csgBox").size(1.2f);
 * auto& csgSphere = chain.add<SphereGeometry>("csgSphere").radius(0.8f);
 * scene.add<Boolean>("hollow")
 *     .inputA(&csgBox).inputB(&csgSphere).operation(BooleanOp::Subtract);
 *
 * auto& render = chain.add<Render3D>("render").input(&scene);
 * @endcode
 */
class SceneComposer : public GeometryOperator {
public:
    /**
     * @brief Create a SceneComposer and register it with the chain
     * @param chain The chain to register with
     * @param name Unique name for this operator
     * @return Reference to the new SceneComposer
     *
     * This is the recommended way to create a SceneComposer. It allows
     * using the add<T>() method to create geometry that's automatically
     * registered with the chain.
     */
    static SceneComposer& create(Chain& chain, const std::string& name) {
        auto& sc = chain.add<SceneComposer>(name);
        sc.m_chain = &chain;
        return sc;
    }

    /**
     * @brief Create and add a geometry operator to the scene
     * @tparam T GeometryOperator type (e.g., BoxGeometry, SphereGeometry)
     * @param name Unique name for the geometry operator
     * @param transform Model transform matrix (default: identity)
     * @param color RGBA color (default: white)
     * @return Reference to the new geometry operator for configuration
     *
     * This creates the geometry operator, registers it with the chain,
     * and adds it to this scene in one step.
     *
     * @par Example
     * @code
     * scene.add<BoxGeometry>("box", transform, color)
     *     .size(1.0f)
     *     .flatShading(true);
     * @endcode
     */
    template<typename T>
    T& add(const std::string& name,
           const glm::mat4& transform = glm::mat4(1.0f),
           const glm::vec4& color = glm::vec4(1.0f)) {
        if (!m_chain) {
            throw std::runtime_error("SceneComposer: must use SceneComposer::create() to enable add<T>()");
        }

        // Create and register the geometry operator
        T& geom = m_chain->add<T>(name);

        // Add to our entries
        ComposerEntry entry;
        entry.geometry = &geom;
        entry.transform = transform;
        entry.color = color;
        entry.inputIndex = static_cast<int>(inputs_.size());
        setInput(entry.inputIndex, &geom);
        m_entries.push_back(entry);

        return geom;
    }

    /**
     * @brief Add a geometry with identity transform and white color
     * @param op GeometryOperator to add
     * @return Reference to this for chaining
     */
    SceneComposer& add(GeometryOperator* op) {
        return add(op, glm::mat4(1.0f), glm::vec4(1.0f));
    }

    /**
     * @brief Add a geometry with transform
     * @param op GeometryOperator to add
     * @param transform Model transform matrix
     * @return Reference to this for chaining
     */
    SceneComposer& add(GeometryOperator* op, const glm::mat4& transform) {
        return add(op, transform, glm::vec4(1.0f));
    }

    /**
     * @brief Add a geometry with transform and color
     * @param op GeometryOperator to add
     * @param transform Model transform matrix
     * @param color RGBA color
     * @return Reference to this for chaining
     */
    SceneComposer& add(GeometryOperator* op, const glm::mat4& transform, const glm::vec4& color) {
        ComposerEntry entry;
        entry.geometry = op;
        entry.transform = transform;
        entry.color = color;
        entry.inputIndex = static_cast<int>(inputs_.size());

        // Register as input for dependency tracking
        setInput(entry.inputIndex, op);

        m_entries.push_back(entry);
        return *this;
    }

    /**
     * @brief Update transform for an entry by index
     * @param index Entry index (order added)
     * @param transform New transform matrix
     * @return Reference to this for chaining
     */
    SceneComposer& setTransform(size_t index, const glm::mat4& transform) {
        if (index < m_entries.size()) {
            m_entries[index].transform = transform;
        }
        return *this;
    }

    /**
     * @brief Update color for an entry by index
     * @param index Entry index (order added)
     * @param color New RGBA color
     * @return Reference to this for chaining
     */
    SceneComposer& setColor(size_t index, const glm::vec4& color) {
        if (index < m_entries.size()) {
            m_entries[index].color = color;
        }
        return *this;
    }

    /**
     * @brief Get the composed scene
     * @return Reference to the output scene
     *
     * Call this after process() to get the composed scene for rendering.
     */
    Scene& outputScene() { return m_scene; }
    const Scene& outputScene() const { return m_scene; }

    /**
     * @brief Get mutable access to entries for animation
     * @return Reference to entries vector
     */
    std::vector<ComposerEntry>& entries() { return m_entries; }
    const std::vector<ComposerEntry>& entries() const { return m_entries; }

    /**
     * @brief Returns nullptr (SceneComposer outputs a Scene, not a Mesh)
     */
    Mesh* outputMesh() override { return nullptr; }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        m_scene.clear();

        for (auto& entry : m_entries) {
            if (entry.geometry) {
                Mesh* mesh = entry.geometry->outputMesh();
                if (mesh) {
                    m_scene.add(*mesh, entry.transform, entry.color);
                }
            }
        }
    }

    void cleanup() override {
        m_scene.clear();
        m_entries.clear();
    }

    std::string name() const override { return "SceneComposer"; }

private:
    Chain* m_chain = nullptr;  // For add<T>() method
    std::vector<ComposerEntry> m_entries;
    Scene m_scene;
};

} // namespace vivid::render3d
