#pragma once

#include <vivid/graphics3d.h>
#include <vivid/animation.h>
#include <string>
#include <vector>

namespace vivid::models {

/**
 * @brief Result of parsing a static 3D model.
 */
struct ParsedMesh {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    bool valid() const { return !vertices.empty() && !indices.empty(); }
};

/**
 * @brief Result of parsing a skinned 3D model with skeleton and animations.
 */
struct ParsedSkinnedMesh {
    std::vector<SkinnedVertex3D> vertices;
    std::vector<uint32_t> indices;
    Skeleton skeleton;
    std::vector<AnimationClip> animations;

    bool valid() const { return !vertices.empty() && !indices.empty(); }
    bool hasAnimations() const { return !animations.empty(); }
};

/**
 * @brief Parse a static 3D model file.
 *
 * Supports many formats including:
 * - FBX (.fbx)
 * - OBJ (.obj)
 * - glTF/GLB (.gltf, .glb)
 * - COLLADA (.dae)
 * - 3DS (.3ds)
 * - And many more...
 *
 * All meshes in the file are combined into a single mesh.
 * Generates tangents for normal mapping if not present.
 *
 * @param path Path to the model file.
 * @return ParsedMesh containing vertex and index data.
 */
ParsedMesh parseModel(const std::string& path);

/**
 * @brief Parse a skinned 3D model with skeleton and animations.
 *
 * Extracts bone hierarchy, vertex weights, and animation clips from
 * FBX files and other formats that support skeletal animation.
 *
 * @param path Path to the model file.
 * @return ParsedSkinnedMesh containing geometry, skeleton, and animations.
 */
ParsedSkinnedMesh parseSkinnedModel(const std::string& path);

/**
 * @brief Check if a file extension is supported for model loading.
 * @param path Path to check.
 * @return true if the extension is a supported model format.
 */
bool isFormatSupported(const std::string& path);

/**
 * @brief Get a list of supported model file extensions.
 * @return Vector of extensions (e.g., ".fbx", ".obj", ".gltf").
 */
std::vector<std::string> getSupportedExtensions();

} // namespace vivid::models
