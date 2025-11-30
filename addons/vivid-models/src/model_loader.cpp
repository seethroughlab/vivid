#include <vivid/models/model_loader.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <algorithm>
#include <unordered_set>

namespace vivid::models {

// Supported file extensions
static const std::vector<std::string> SUPPORTED_EXTENSIONS = {
    ".fbx", ".obj", ".gltf", ".glb", ".dae", ".3ds", ".blend",
    ".ply", ".stl", ".x", ".ms3d", ".cob", ".scn", ".xgl",
    ".b3d", ".lwo", ".lws", ".ase", ".dxf", ".hmp", ".md2",
    ".md3", ".md5mesh", ".smd", ".vta", ".ogex", ".3d", ".ac",
    ".bvh", ".csm", ".irrmesh", ".irr", ".mdl", ".nff", ".off",
    ".pk3", ".raw", ".ter", ".xml"
};

bool isFormatSupported(const std::string& path) {
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos) return false;

    std::string ext = path.substr(dotPos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    for (const auto& supported : SUPPORTED_EXTENSIONS) {
        if (ext == supported) return true;
    }
    return false;
}

std::vector<std::string> getSupportedExtensions() {
    return SUPPORTED_EXTENSIONS;
}

// Convert Assimp matrix to GLM
static glm::mat4 aiToGlm(const aiMatrix4x4& m) {
    return glm::mat4(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4
    );
}

// Process a single Assimp mesh for static models
static void processMesh(const aiMesh* mesh, const aiMatrix4x4& transform,
                        std::vector<Vertex3D>& vertices,
                        std::vector<uint32_t>& indices) {
    uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

    // Convert transform to column-major for GLM
    glm::mat4 mat = aiToGlm(transform);
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(mat)));

    // Process vertices
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        Vertex3D vertex;

        // Position (apply transform)
        aiVector3D pos = mesh->mVertices[i];
        glm::vec4 transformedPos = mat * glm::vec4(pos.x, pos.y, pos.z, 1.0f);
        vertex.position = glm::vec3(transformedPos);

        // Normal (apply normal matrix)
        if (mesh->HasNormals()) {
            aiVector3D norm = mesh->mNormals[i];
            vertex.normal = glm::normalize(normalMat * glm::vec3(norm.x, norm.y, norm.z));
        } else {
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        // Texture coordinates (first UV channel)
        if (mesh->HasTextureCoords(0)) {
            vertex.uv = glm::vec2(mesh->mTextureCoords[0][i].x,
                                   mesh->mTextureCoords[0][i].y);
        } else {
            vertex.uv = glm::vec2(0.0f);
        }

        // Tangent
        if (mesh->HasTangentsAndBitangents()) {
            aiVector3D tan = mesh->mTangents[i];
            aiVector3D bitan = mesh->mBitangents[i];

            glm::vec3 t = glm::normalize(normalMat * glm::vec3(tan.x, tan.y, tan.z));
            glm::vec3 b = glm::normalize(normalMat * glm::vec3(bitan.x, bitan.y, bitan.z));

            float handedness = (glm::dot(glm::cross(vertex.normal, t), b) < 0.0f) ? -1.0f : 1.0f;
            vertex.tangent = glm::vec4(t, handedness);
        } else {
            vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        }

        vertices.push_back(vertex);
    }

    // Process indices
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            indices.push_back(baseIndex + face.mIndices[j]);
        }
    }
}

// Recursively process nodes for static models
static void processNode(const aiNode* node, const aiScene* scene,
                        const aiMatrix4x4& parentTransform,
                        std::vector<Vertex3D>& vertices,
                        std::vector<uint32_t>& indices) {
    aiMatrix4x4 nodeTransform = parentTransform * node->mTransformation;

    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        processMesh(mesh, nodeTransform, vertices, indices);
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        processNode(node->mChildren[i], scene, nodeTransform, vertices, indices);
    }
}

ParsedMesh parseModel(const std::string& path) {
    ParsedMesh result;

    Assimp::Importer importer;

    unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_SortByPType |
        aiProcess_FlipUVs |
        aiProcess_ValidateDataStructure;

    const aiScene* scene = importer.ReadFile(path, flags);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "[vivid-models] Failed to load: " << path << "\n";
        std::cerr << "[vivid-models] Error: " << importer.GetErrorString() << "\n";
        return result;
    }

    aiMatrix4x4 identity;
    processNode(scene->mRootNode, scene, identity, result.vertices, result.indices);

    if (result.vertices.empty() || result.indices.empty()) {
        std::cerr << "[vivid-models] No geometry found in: " << path << "\n";
        return ParsedMesh{};
    }

    std::cout << "[vivid-models] Loaded " << path << "\n";
    std::cout << "[vivid-models]   " << result.vertices.size() << " vertices, "
              << result.indices.size() / 3 << " triangles\n";

    return result;
}

// Collect all bone names from meshes
static std::unordered_set<std::string> collectBoneNames(const aiScene* scene) {
    std::unordered_set<std::string> names;
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
            names.insert(mesh->mBones[b]->mName.C_Str());
        }
    }
    return names;
}

// Build skeleton from scene node hierarchy
static void buildSkeletonFromNode(const aiNode* node, const aiScene* scene,
                                   Skeleton& skeleton, int parentBoneIndex,
                                   const glm::mat4& accumulatedTransform,
                                   const std::unordered_set<std::string>& boneNames) {
    std::string nodeName(node->mName.C_Str());
    glm::mat4 nodeTransform = aiToGlm(node->mTransformation);

    if (boneNames.count(nodeName) > 0) {
        Bone bone;
        bone.name = nodeName;
        bone.parentIndex = parentBoneIndex;
        bone.preTransform = accumulatedTransform;
        bone.localTransform = nodeTransform;
        int boneIndex = skeleton.addBone(bone);

        for (unsigned int i = 0; i < node->mNumChildren; ++i) {
            buildSkeletonFromNode(node->mChildren[i], scene, skeleton, boneIndex,
                                  glm::mat4(1.0f), boneNames);
        }
    } else {
        glm::mat4 newAccumulated = accumulatedTransform * nodeTransform;
        for (unsigned int i = 0; i < node->mNumChildren; ++i) {
            buildSkeletonFromNode(node->mChildren[i], scene, skeleton, parentBoneIndex,
                                  newAccumulated, boneNames);
        }
    }
}

// Process skinned mesh with bone weights
static void processSkinnedMesh(const aiMesh* mesh, const aiScene* scene,
                               std::vector<SkinnedVertex3D>& vertices,
                               std::vector<uint32_t>& indices,
                               Skeleton& skeleton) {
    uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

    // Process vertices
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        SkinnedVertex3D vertex;

        aiVector3D pos = mesh->mVertices[i];
        vertex.position = glm::vec3(pos.x, pos.y, pos.z);

        if (mesh->HasNormals()) {
            aiVector3D norm = mesh->mNormals[i];
            vertex.normal = glm::vec3(norm.x, norm.y, norm.z);
        }

        if (mesh->HasTextureCoords(0)) {
            vertex.uv = glm::vec2(mesh->mTextureCoords[0][i].x,
                                   mesh->mTextureCoords[0][i].y);
        }

        if (mesh->HasTangentsAndBitangents()) {
            aiVector3D tan = mesh->mTangents[i];
            aiVector3D bitan = mesh->mBitangents[i];
            glm::vec3 t(tan.x, tan.y, tan.z);
            glm::vec3 b(bitan.x, bitan.y, bitan.z);
            float handedness = (glm::dot(glm::cross(vertex.normal, t), b) < 0.0f) ? -1.0f : 1.0f;
            vertex.tangent = glm::vec4(t, handedness);
        } else {
            vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        }

        vertices.push_back(vertex);
    }

    // Process bone weights
    for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
        const aiBone* bone = mesh->mBones[b];
        std::string boneName(bone->mName.C_Str());

        int boneIndex = skeleton.findBone(boneName);
        if (boneIndex < 0) {
            Bone newBone;
            newBone.name = boneName;
            newBone.offsetMatrix = aiToGlm(bone->mOffsetMatrix);
            boneIndex = skeleton.addBone(newBone);
        } else {
            skeleton.bones[boneIndex].offsetMatrix = aiToGlm(bone->mOffsetMatrix);
        }

        for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
            unsigned int vertexId = bone->mWeights[w].mVertexId;
            float weight = bone->mWeights[w].mWeight;

            if (baseIndex + vertexId < vertices.size()) {
                vertices[baseIndex + vertexId].addBoneInfluence(boneIndex, weight);
            }
        }
    }

    // Normalize bone weights
    for (uint32_t i = baseIndex; i < vertices.size(); ++i) {
        vertices[i].normalizeBoneWeights();
    }

    // Process indices
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            indices.push_back(baseIndex + face.mIndices[j]);
        }
    }
}

// Extract animations from scene
static void extractAnimations(const aiScene* scene, Skeleton& skeleton,
                               std::vector<AnimationClip>& animations) {
    for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
        const aiAnimation* anim = scene->mAnimations[a];

        AnimationClip clip;
        clip.name = anim->mName.C_Str();
        if (clip.name.empty()) {
            clip.name = "Animation_" + std::to_string(a);
        }

        clip.ticksPerSecond = (anim->mTicksPerSecond > 0) ?
                               static_cast<float>(anim->mTicksPerSecond) : 25.0f;
        clip.duration = static_cast<float>(anim->mDuration) / clip.ticksPerSecond;

        for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
            const aiNodeAnim* channel = anim->mChannels[c];

            AnimationChannel animChannel;
            animChannel.boneName = channel->mNodeName.C_Str();
            animChannel.boneIndex = skeleton.findBone(animChannel.boneName);

            // Position keyframes
            for (unsigned int k = 0; k < channel->mNumPositionKeys; ++k) {
                const aiVectorKey& key = channel->mPositionKeys[k];
                float time = static_cast<float>(key.mTime) / clip.ticksPerSecond;
                animChannel.positionKeys.push_back({
                    time,
                    glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z)
                });
            }

            // Rotation keyframes
            for (unsigned int k = 0; k < channel->mNumRotationKeys; ++k) {
                const aiQuatKey& key = channel->mRotationKeys[k];
                float time = static_cast<float>(key.mTime) / clip.ticksPerSecond;
                animChannel.rotationKeys.push_back({
                    time,
                    glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z)
                });
            }

            // Scale keyframes
            for (unsigned int k = 0; k < channel->mNumScalingKeys; ++k) {
                const aiVectorKey& key = channel->mScalingKeys[k];
                float time = static_cast<float>(key.mTime) / clip.ticksPerSecond;
                animChannel.scaleKeys.push_back({
                    time,
                    glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z)
                });
            }

            clip.channels.push_back(std::move(animChannel));
        }

        animations.push_back(std::move(clip));
    }
}

ParsedSkinnedMesh parseSkinnedModel(const std::string& path) {
    ParsedSkinnedMesh result;

    Assimp::Importer importer;

    unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_LimitBoneWeights |
        aiProcess_FlipUVs |
        aiProcess_ValidateDataStructure;

    const aiScene* scene = importer.ReadFile(path, flags);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "[vivid-models] Failed to load: " << path << "\n";
        std::cerr << "[vivid-models] Error: " << importer.GetErrorString() << "\n";
        return result;
    }

    // Collect bone names
    auto boneNames = collectBoneNames(scene);

    // Build skeleton hierarchy
    buildSkeletonFromNode(scene->mRootNode, scene, result.skeleton, -1, glm::mat4(1.0f), boneNames);

    // Process all meshes
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        processSkinnedMesh(mesh, scene, result.vertices, result.indices, result.skeleton);
    }

    // Extract animations
    extractAnimations(scene, result.skeleton, result.animations);

    // Link animation channels to skeleton
    for (auto& clip : result.animations) {
        clip.linkToSkeleton(result.skeleton);
    }

    if (result.vertices.empty()) {
        std::cerr << "[vivid-models] No geometry found in: " << path << "\n";
        return ParsedSkinnedMesh{};
    }

    std::cout << "[vivid-models] Loaded skinned model: " << path << "\n";
    std::cout << "[vivid-models]   " << result.vertices.size() << " vertices, "
              << result.indices.size() / 3 << " triangles\n";
    std::cout << "[vivid-models]   " << result.skeleton.bones.size() << " bones, "
              << result.animations.size() << " animation(s)\n";

    return result;
}

} // namespace vivid::models
