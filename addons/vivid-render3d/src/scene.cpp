#include <vivid/render3d/scene.h>

namespace vivid::render3d {

Scene& Scene::add(Mesh& mesh) {
    m_objects.emplace_back(&mesh);
    return *this;
}

Scene& Scene::add(Mesh& mesh, const glm::mat4& transform) {
    m_objects.emplace_back(&mesh, transform);
    return *this;
}

Scene& Scene::add(Mesh& mesh, const glm::mat4& transform, const glm::vec4& color) {
    m_objects.emplace_back(&mesh, transform, color);
    return *this;
}

Scene& Scene::add(const SceneObject& object) {
    m_objects.push_back(object);
    return *this;
}

void Scene::clear() {
    m_objects.clear();
}

} // namespace vivid::render3d
