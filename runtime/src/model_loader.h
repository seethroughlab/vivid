#pragma once

#include <vivid/graphics3d.h>
#include <vivid/animation.h>
#include <string>
#include <vector>

namespace vivid {

/**
 * @brief Load a 3D model file using Assimp.
 *
 * Supports many formats including:
 * - FBX (.fbx)
 * - OBJ (.obj)
 * - glTF/GLB (.gltf, .glb)
 * - COLLADA (.dae)
 * - 3DS (.3ds)
 * - Blender (.blend)
 * - And many more...
 *
 * All meshes in the file are combined into a single mesh.
 * Generates tangents for normal mapping if not present.
 *
 * @param path Path to the model file.
 * @param vertices Output vertex data.
 * @param indices Output index data.
 * @return true if load succeeded.
 */
bool loadModel(const std::string& path,
               std::vector<Vertex3D>& vertices,
               std::vector<uint32_t>& indices);

/**
 * @brief Check if a file extension is supported for model loading.
 * @param path Path to check.
 * @return true if the extension is a supported model format.
 */
bool isModelSupported(const std::string& path);

/**
 * @brief Get a list of supported model file extensions.
 * @return Vector of extensions (e.g., ".fbx", ".obj", ".gltf").
 */
std::vector<std::string> getSupportedModelExtensions();

/**
 * @brief Load a skinned 3D model with skeleton and animations.
 *
 * Extracts bone hierarchy, vertex weights, and animation clips from
 * FBX files and other formats that support skeletal animation.
 *
 * @param path Path to the model file.
 * @param vertices Output skinned vertex data (with bone weights).
 * @param indices Output index data.
 * @param skeleton Output skeleton with bone hierarchy.
 * @param animations Output animation clips.
 * @return true if load succeeded (may still have empty skeleton/animations).
 */
bool loadSkinnedModel(const std::string& path,
                      std::vector<SkinnedVertex3D>& vertices,
                      std::vector<uint32_t>& indices,
                      Skeleton& skeleton,
                      std::vector<AnimationClip>& animations);

} // namespace vivid
