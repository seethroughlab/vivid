#pragma once

/**
 * @file scene_composer.h
 * @brief Composes multiple geometries into a renderable scene
 *
 * SceneComposer takes geometry inputs and combines them with transforms
 * and colors into a Scene that can be rendered by Render3D.
 */

#include <vivid/render3d/mesh_operator.h>
#include <vivid/render3d/static_mesh.h>
#include <vivid/render3d/scene.h>
#include <vivid/render3d/textured_material.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace vivid::render3d {

/**
 * @brief Entry for a mesh in the composer
 */
struct ComposerEntry {
    MeshOperator* geometry = nullptr;
    glm::mat4 transform = glm::mat4(1.0f);
    glm::vec4 color = glm::vec4(1.0f);
    TexturedMaterial* material = nullptr;  // Optional per-object material
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
 * @par Example (Recommended - SceneComposer manages meshes)
 * @code
 * auto& scene = SceneComposer::create(chain, "scene");
 *
 * // Add meshes - automatically registered in chain
 * scene.add<Box>("box")
 *     .size(1.0f);
 *
 * scene.add<Sphere>("sphere", glm::translate(glm::mat4(1.0f), glm::vec3(2, 0, 0)))
 *     .radius(0.5f);
 *
 * // For CSG, create inputs via chain (not added to scene)
 * auto& csgBox = chain.add<Box>("csgBox").size(1.2f);
 * auto& csgSphere = chain.add<Sphere>("csgSphere").radius(0.8f);
 * scene.add<Boolean>("hollow")
 *     .inputA(&csgBox).inputB(&csgSphere).operation(BooleanOp::Subtract);
 *
 * auto& render = chain.add<Render3D>("render").input(&scene);
 * @endcode
 */
class SceneComposer : public MeshOperator {
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
     * @brief Create and add a mesh operator to the scene
     * @tparam T MeshOperator type (e.g., Box, Sphere)
     * @param name Unique name for the mesh operator
     * @param transform Model transform matrix (default: identity)
     * @param color RGBA color (default: white)
     * @return Reference to the new mesh operator for configuration
     *
     * This creates the mesh operator, registers it with the chain,
     * and adds it to this scene in one step.
     *
     * @par Example
     * @code
     * scene.add<Box>("box", transform, color)
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
     * @brief Add a mesh with identity transform and white color
     * @param op MeshOperator to add
     */
    void add(MeshOperator* op) {
        add(op, glm::mat4(1.0f), glm::vec4(1.0f));
    }

    /**
     * @brief Add a mesh with transform
     * @param op MeshOperator to add
     * @param transform Model transform matrix
     */
    void add(MeshOperator* op, const glm::mat4& transform) {
        add(op, transform, glm::vec4(1.0f));
    }

    /**
     * @brief Add a mesh with transform and color
     * @param op MeshOperator to add
     * @param transform Model transform matrix
     * @param color RGBA color
     */
    void add(MeshOperator* op, const glm::mat4& transform, const glm::vec4& color) {
        ComposerEntry entry;
        entry.geometry = op;
        entry.transform = transform;
        entry.color = color;
        entry.inputIndex = static_cast<int>(inputs_.size());

        // Register as input for dependency tracking
        setInput(entry.inputIndex, op);

        m_entries.push_back(entry);
    }

    /**
     * @brief Add a mesh with a textured material
     * @param op MeshOperator to add
     * @param material TexturedMaterial for this object
     * @return Reference to an EntryBuilder for additional configuration
     */
    class EntryBuilder {
    public:
        EntryBuilder(SceneComposer& composer, size_t index)
            : m_composer(composer), m_index(index) {}

        /// Set transform matrix
        void setTransform(const glm::mat4& t) {
            m_composer.m_entries[m_index].transform = t;
        }

        /// Set color
        void setColor(const glm::vec4& c) {
            m_composer.m_entries[m_index].color = c;
        }

        /// Set color (convenience)
        void setColor(float r, float g, float b, float a = 1.0f) {
            m_composer.m_entries[m_index].color = glm::vec4(r, g, b, a);
        }

    private:
        SceneComposer& m_composer;
        size_t m_index;
    };

    EntryBuilder add(MeshOperator* op, TexturedMaterial* material) {
        ComposerEntry entry;
        entry.geometry = op;
        entry.material = material;
        entry.inputIndex = static_cast<int>(inputs_.size());

        // Register geometry as input for dependency tracking
        setInput(entry.inputIndex, op);

        // Register material as input too
        if (material) {
            setInput(static_cast<int>(inputs_.size()), material);
        }

        m_entries.push_back(entry);
        return EntryBuilder(*this, m_entries.size() - 1);
    }

    /**
     * @brief Add a custom mesh to the scene (registered in chain)
     * @param name Unique name for the mesh operator
     * @param mesh Pre-built mesh (ownership transferred)
     * @param transform Model transform matrix (default: identity)
     * @param color RGBA color (default: white)
     * @return Reference to the StaticMesh for further configuration
     *
     * This creates a StaticMesh, registers it with the chain, and adds
     * it to this scene. The mesh will appear in the chain visualizer.
     *
     * @par Example
     * @code
     * auto builder = MeshBuilder::box(1.0f, 1.0f, 1.0f);
     * builder.computeFlatNormals();
     *
     * scene.addMesh("customBox", builder,
     *               glm::translate(glm::mat4(1.0f), glm::vec3(2, 0, 0)),
     *               glm::vec4(1.0f, 0.5f, 0.3f, 1.0f));
     * @endcode
     */
    StaticMesh& addMesh(const std::string& name,
                        Mesh&& mesh,
                        const glm::mat4& transform = glm::mat4(1.0f),
                        const glm::vec4& color = glm::vec4(1.0f)) {
        if (!m_chain) {
            throw std::runtime_error("SceneComposer: must use SceneComposer::create() to enable addMesh()");
        }

        // Create and register the StaticMesh
        StaticMesh& meshOp = m_chain->add<StaticMesh>(name);
        meshOp.setMesh(std::move(mesh));

        // Add to our entries
        ComposerEntry entry;
        entry.geometry = &meshOp;
        entry.transform = transform;
        entry.color = color;
        entry.inputIndex = static_cast<int>(inputs_.size());
        setInput(entry.inputIndex, &meshOp);
        m_entries.push_back(entry);

        return meshOp;
    }

    /**
     * @brief Add a mesh built from a MeshBuilder (convenience overload)
     * @param name Unique name for the mesh operator
     * @param builder MeshBuilder containing the geometry
     * @param transform Model transform matrix (default: identity)
     * @param color RGBA color (default: white)
     * @return Reference to the StaticMesh for further configuration
     */
    StaticMesh& addMesh(const std::string& name,
                        MeshBuilder& builder,
                        const glm::mat4& transform = glm::mat4(1.0f),
                        const glm::vec4& color = glm::vec4(1.0f)) {
        if (!m_chain) {
            throw std::runtime_error("SceneComposer: must use SceneComposer::create() to enable addMesh()");
        }

        // Create and register the StaticMesh
        StaticMesh& meshOp = m_chain->add<StaticMesh>(name);
        meshOp.setMesh(builder);

        // Add to our entries
        ComposerEntry entry;
        entry.geometry = &meshOp;
        entry.transform = transform;
        entry.color = color;
        entry.inputIndex = static_cast<int>(inputs_.size());
        setInput(entry.inputIndex, &meshOp);
        m_entries.push_back(entry);

        return meshOp;
    }

    /**
     * @brief Set root transform applied to all entries
     * @param transform Transform matrix applied before each entry's local transform
     *
     * The root transform is multiplied with each entry's transform during process().
     * This is useful for applying a single transform to the entire scene (e.g., hover animation).
     *
     * @par Example
     * @code
     * // Apply hover animation to entire craft
     * glm::mat4 hover = glm::translate(glm::mat4(1.0f), glm::vec3(0, sin(time) * 0.1f, 0));
     * scene.setRootTransform(hover);
     * @endcode
     */
    void setRootTransform(const glm::mat4& transform) {
        m_rootTransform = transform;
    }

    /**
     * @brief Get the current root transform
     */
    const glm::mat4& rootTransform() const { return m_rootTransform; }

    /**
     * @brief Update transform for an entry by index
     * @param index Entry index (order added)
     * @param transform New transform matrix
     */
    void setEntryTransform(size_t index, const glm::mat4& transform) {
        if (index < m_entries.size()) {
            m_entries[index].transform = transform;
        }
    }

    /**
     * @brief Update color for an entry by index
     * @param index Entry index (order added)
     * @param color New RGBA color
     */
    void setEntryColor(size_t index, const glm::vec4& color) {
        if (index < m_entries.size()) {
            m_entries[index].color = color;
        }
    }

    /**
     * @brief Set material for an entry by index with dependency tracking
     * @param index Entry index (order added)
     * @param material TexturedMaterial to use (nullptr for default material)
     *
     * Use this instead of directly setting entries()[i].material to ensure
     * proper dependency tracking (scene updates when material's inputs change).
     */
    void setEntryMaterial(size_t index, TexturedMaterial* material) {
        if (index < m_entries.size()) {
            m_entries[index].material = material;
            if (material) {
                setInput(static_cast<int>(inputs_.size()), material);
            }
            markDirty();
        }
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
        if (!needsCook()) return;

        m_scene.clear();

        for (auto& entry : m_entries) {
            if (entry.geometry) {
                Mesh* mesh = entry.geometry->outputMesh();
                if (mesh) {
                    // Get material: prefer explicit entry.material, fallback to geometry's outputMaterial()
                    TexturedMaterial* mat = entry.material;
                    if (!mat) {
                        mat = entry.geometry->outputMaterial();
                    }

                    // Apply root transform before entry's local transform
                    glm::mat4 finalTransform = m_rootTransform * entry.transform;

                    // Add to scene with material
                    SceneObject obj(mesh, finalTransform, entry.color, mat);
                    m_scene.objects().push_back(obj);
                }
            }
        }

        didCook();
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
    glm::mat4 m_rootTransform = glm::mat4(1.0f);
};

} // namespace vivid::render3d
