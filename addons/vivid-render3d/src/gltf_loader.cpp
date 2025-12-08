// GLTF Loader Implementation
// Uses cgltf for parsing GLTF/GLB files

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <vivid/render3d/gltf_loader.h>
#include <vivid/io/image_loader.h>
#include <vivid/context.h>
#include <iostream>
#include <fstream>
#include <cstring>

namespace vivid::render3d {

GLTFLoader::GLTFLoader() = default;
GLTFLoader::~GLTFLoader() = default;

GLTFLoader& GLTFLoader::file(const std::string& path) {
    if (m_filePath != path) {
        m_filePath = path;
        m_needsLoad = true;
        m_loaded = false;

        // Extract base directory for relative texture paths
        size_t lastSlash = path.find_last_of("/\\");
        m_baseDir = (lastSlash != std::string::npos) ? path.substr(0, lastSlash + 1) : "";
    }
    return *this;
}

GLTFLoader& GLTFLoader::meshIndex(int index) {
    if (m_meshIndex != index) {
        m_meshIndex = index;
        m_needsLoad = true;
    }
    return *this;
}

GLTFLoader& GLTFLoader::loadTextures(bool enabled) {
    m_loadTextures = enabled;
    return *this;
}

GLTFLoader& GLTFLoader::scale(float s) {
    m_scale = s;
    return *this;
}

GLTFLoader& GLTFLoader::computeTangents(bool enabled) {
    m_computeTangents = enabled;
    return *this;
}

void GLTFLoader::init(Context& ctx) {
    if (m_needsLoad && !m_filePath.empty()) {
        loadGLTF(ctx);
    }
}

void GLTFLoader::process(Context& ctx) {
    if (m_needsLoad && !m_filePath.empty()) {
        loadGLTF(ctx);
    }
}

void GLTFLoader::cleanup() {
    m_mesh.release();
    m_material.reset();
    m_loaded = false;
}

bool GLTFLoader::loadGLTF(Context& ctx) {
    m_needsLoad = false;
    m_loaded = false;
    m_error.clear();
    m_mesh.vertices.clear();
    m_mesh.indices.clear();

    // Reset material for new model
    if (m_material) {
        m_material->cleanup();
        m_material.reset();
    }

    // Parse GLTF file
    cgltf_options options = {};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, m_filePath.c_str(), &data);
    if (result != cgltf_result_success) {
        m_error = "Failed to parse GLTF file: " + m_filePath;
        std::cerr << "[GLTFLoader] " << m_error << std::endl;
        return false;
    }

    // Load buffers (for binary data)
    result = cgltf_load_buffers(&options, data, m_filePath.c_str());
    if (result != cgltf_result_success) {
        m_error = "Failed to load GLTF buffers";
        std::cerr << "[GLTFLoader] " << m_error << std::endl;
        cgltf_free(data);
        return false;
    }

    // Check for meshes
    if (data->meshes_count == 0) {
        m_error = "GLTF file contains no meshes";
        std::cerr << "[GLTFLoader] " << m_error << std::endl;
        cgltf_free(data);
        return false;
    }

    // Determine which meshes to load
    size_t startMesh = 0;
    size_t endMesh = data->meshes_count;

    // If specific mesh requested, only load that one
    if (m_meshIndex >= 0 && static_cast<size_t>(m_meshIndex) < data->meshes_count) {
        startMesh = static_cast<size_t>(m_meshIndex);
        endMesh = startMesh + 1;
    }

    // Process all meshes (or just the selected one)
    for (size_t meshIdx = startMesh; meshIdx < endMesh; ++meshIdx) {
        cgltf_mesh* mesh = &data->meshes[meshIdx];

        // Process all primitives in this mesh
        for (size_t primIdx = 0; primIdx < mesh->primitives_count; ++primIdx) {
            cgltf_primitive* primitive = &mesh->primitives[primIdx];

        if (primitive->type != cgltf_primitive_type_triangles) {
            continue;  // Only support triangle primitives
        }

        // Get accessor for each attribute
        cgltf_accessor* posAccessor = nullptr;
        cgltf_accessor* normAccessor = nullptr;
        cgltf_accessor* uvAccessor = nullptr;
        cgltf_accessor* tangentAccessor = nullptr;

        for (size_t attrIdx = 0; attrIdx < primitive->attributes_count; ++attrIdx) {
            cgltf_attribute* attr = &primitive->attributes[attrIdx];
            switch (attr->type) {
                case cgltf_attribute_type_position:
                    posAccessor = attr->data;
                    break;
                case cgltf_attribute_type_normal:
                    normAccessor = attr->data;
                    break;
                case cgltf_attribute_type_texcoord:
                    if (!uvAccessor) uvAccessor = attr->data;  // Use first UV set
                    break;
                case cgltf_attribute_type_tangent:
                    tangentAccessor = attr->data;
                    break;
                default:
                    break;
            }
        }

        if (!posAccessor) {
            continue;  // Need at least positions
        }

        // Base index for this primitive
        uint32_t baseVertex = static_cast<uint32_t>(m_mesh.vertices.size());

        // Read vertices
        size_t vertexCount = posAccessor->count;
        for (size_t i = 0; i < vertexCount; ++i) {
            Vertex3D vertex;

            // Position (required)
            float pos[3];
            cgltf_accessor_read_float(posAccessor, i, pos, 3);
            vertex.position = glm::vec3(pos[0], pos[1], pos[2]) * m_scale;

            // Normal (optional)
            if (normAccessor) {
                float norm[3];
                cgltf_accessor_read_float(normAccessor, i, norm, 3);
                vertex.normal = glm::vec3(norm[0], norm[1], norm[2]);
            }

            // UV (optional)
            if (uvAccessor) {
                float uv[2];
                cgltf_accessor_read_float(uvAccessor, i, uv, 2);
                vertex.uv = glm::vec2(uv[0], uv[1]);
            }

            // Tangent (optional)
            if (tangentAccessor) {
                float tan[4];
                cgltf_accessor_read_float(tangentAccessor, i, tan, 4);
                vertex.tangent = glm::vec4(tan[0], tan[1], tan[2], tan[3]);
            }

            // Default white color
            vertex.color = glm::vec4(1.0f);

            m_mesh.vertices.push_back(vertex);
        }

        // Read indices
        if (primitive->indices) {
            cgltf_accessor* indexAccessor = primitive->indices;
            for (size_t i = 0; i < indexAccessor->count; ++i) {
                uint32_t index = static_cast<uint32_t>(cgltf_accessor_read_index(indexAccessor, i));
                m_mesh.indices.push_back(baseVertex + index);
            }
        } else {
            // No indices - generate sequential indices
            for (size_t i = 0; i < vertexCount; ++i) {
                m_mesh.indices.push_back(baseVertex + static_cast<uint32_t>(i));
            }
        }

        // Load material textures (first primitive's material)
        if (m_loadTextures && primitive->material && !m_material) {
            cgltf_material* mat = primitive->material;

            m_material = std::make_unique<TexturedMaterial>();

            // Helper lambda to load a texture (embedded or external)
            auto loadGLTFTexture = [this, data](cgltf_image* image) -> io::ImageData {
                if (!image) return {};

                // Check for embedded texture (GLB format)
                if (image->buffer_view) {
                    const uint8_t* bufData = static_cast<const uint8_t*>(
                        cgltf_buffer_view_data(image->buffer_view));
                    size_t bufSize = image->buffer_view->size;

                    if (bufData && bufSize > 0) {
                        return io::loadImageFromMemory(bufData, bufSize);
                    }
                }

                // External texture with URI
                if (image->uri && image->uri[0] != '\0') {
                    // Skip data URIs for now (base64 encoded)
                    if (strncmp(image->uri, "data:", 5) == 0) {
                        std::cerr << "[GLTFLoader] Data URI textures not supported" << std::endl;
                        return {};
                    }

                    std::string texPath = m_baseDir + image->uri;
                    return io::loadImage(texPath);
                }

                return {};
            };

            // PBR Metallic-Roughness workflow
            if (mat->has_pbr_metallic_roughness) {
                cgltf_pbr_metallic_roughness* pbr = &mat->pbr_metallic_roughness;

                // Base color texture
                if (pbr->base_color_texture.texture && pbr->base_color_texture.texture->image) {
                    auto texData = loadGLTFTexture(pbr->base_color_texture.texture->image);
                    if (texData.valid()) {
                        m_material->baseColorFromData(texData);
                    }
                }

                // Metallic-roughness texture (combined: metallic in B, roughness in G)
                if (pbr->metallic_roughness_texture.texture && pbr->metallic_roughness_texture.texture->image) {
                    auto texData = loadGLTFTexture(pbr->metallic_roughness_texture.texture->image);
                    if (texData.valid()) {
                        m_material->metallicFromData(texData);
                        m_material->roughnessFromData(texData);
                    }
                }

                // Set metallic/roughness factors from material
                m_material->metallicFactor(pbr->metallic_factor);
                m_material->roughnessFactor(pbr->roughness_factor);

                // Base color factor (includes alpha for transparency)
                m_material->baseColorFactor(
                    pbr->base_color_factor[0],
                    pbr->base_color_factor[1],
                    pbr->base_color_factor[2],
                    pbr->base_color_factor[3]);
            }

            // Normal map
            if (mat->normal_texture.texture && mat->normal_texture.texture->image) {
                auto texData = loadGLTFTexture(mat->normal_texture.texture->image);
                if (texData.valid()) {
                    m_material->normalFromData(texData);
                }
                m_material->normalScale(mat->normal_texture.scale);
            }

            // Occlusion map
            if (mat->occlusion_texture.texture && mat->occlusion_texture.texture->image) {
                auto texData = loadGLTFTexture(mat->occlusion_texture.texture->image);
                if (texData.valid()) {
                    m_material->aoFromData(texData);
                }
                m_material->aoStrength(mat->occlusion_texture.scale);  // scale = strength for occlusion
            }

            // Emissive map
            if (mat->emissive_texture.texture && mat->emissive_texture.texture->image) {
                auto texData = loadGLTFTexture(mat->emissive_texture.texture->image);
                if (texData.valid()) {
                    m_material->emissiveFromData(texData);
                }
            }
            // Set emissive factor
            m_material->emissiveFactor(
                mat->emissive_factor[0],
                mat->emissive_factor[1],
                mat->emissive_factor[2]);

            // Alpha mode and cutoff
            switch (mat->alpha_mode) {
                case cgltf_alpha_mode_mask:
                    m_material->alphaMode(TexturedMaterial::AlphaMode::Mask);
                    m_material->alphaCutoff(mat->alpha_cutoff);
                    break;
                case cgltf_alpha_mode_blend:
                    m_material->alphaMode(TexturedMaterial::AlphaMode::Blend);
                    break;
                default:
                    m_material->alphaMode(TexturedMaterial::AlphaMode::Opaque);
                    break;
            }

            // Double-sided rendering
            m_material->doubleSided(mat->double_sided);

            // Initialize material
            m_material->init(ctx);
        }
        }  // end primitive loop
    }  // end mesh loop

    cgltf_free(data);

    if (m_mesh.vertices.empty()) {
        m_error = "No vertices loaded from GLTF";
        std::cerr << "[GLTFLoader] " << m_error << std::endl;
        return false;
    }

    // Compute bounding box
    m_bounds = Bounds3D{};
    for (const auto& v : m_mesh.vertices) {
        m_bounds.expand(v.position);
    }

    // Compute tangents if needed and not present in file
    if (m_computeTangents) {
        bool hasTangents = false;
        for (const auto& v : m_mesh.vertices) {
            if (glm::length(glm::vec3(v.tangent)) > 0.01f) {
                hasTangents = true;
                break;
            }
        }
        if (!hasTangents) {
            computeMeshTangents();
        }
    }

    // Upload to GPU
    m_mesh.upload(ctx);
    m_loaded = true;

    std::cout << "[GLTFLoader] Loaded " << m_filePath
              << " (" << m_mesh.vertexCount() << " verts, "
              << m_mesh.indexCount() / 3 << " tris)" << std::endl;

    return true;
}

void GLTFLoader::computeMeshTangents() {
    // MikkTSpace-style tangent computation
    // For each triangle, compute tangent and bitangent, then average

    std::vector<glm::vec3> tangents(m_mesh.vertices.size(), glm::vec3(0));
    std::vector<glm::vec3> bitangents(m_mesh.vertices.size(), glm::vec3(0));

    for (size_t i = 0; i < m_mesh.indices.size(); i += 3) {
        uint32_t i0 = m_mesh.indices[i];
        uint32_t i1 = m_mesh.indices[i + 1];
        uint32_t i2 = m_mesh.indices[i + 2];

        const glm::vec3& p0 = m_mesh.vertices[i0].position;
        const glm::vec3& p1 = m_mesh.vertices[i1].position;
        const glm::vec3& p2 = m_mesh.vertices[i2].position;

        const glm::vec2& uv0 = m_mesh.vertices[i0].uv;
        const glm::vec2& uv1 = m_mesh.vertices[i1].uv;
        const glm::vec2& uv2 = m_mesh.vertices[i2].uv;

        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;

        glm::vec2 deltaUV1 = uv1 - uv0;
        glm::vec2 deltaUV2 = uv2 - uv0;

        float f = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::abs(f) < 1e-6f) continue;
        f = 1.0f / f;

        glm::vec3 tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        glm::vec3 bitangent;
        bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

        tangents[i0] += tangent;
        tangents[i1] += tangent;
        tangents[i2] += tangent;

        bitangents[i0] += bitangent;
        bitangents[i1] += bitangent;
        bitangents[i2] += bitangent;
    }

    // Orthonormalize and store
    for (size_t i = 0; i < m_mesh.vertices.size(); ++i) {
        const glm::vec3& n = m_mesh.vertices[i].normal;
        glm::vec3 t = tangents[i];

        // Gram-Schmidt orthogonalize
        t = glm::normalize(t - n * glm::dot(n, t));

        // Calculate handedness
        float w = (glm::dot(glm::cross(n, t), bitangents[i]) < 0.0f) ? -1.0f : 1.0f;

        m_mesh.vertices[i].tangent = glm::vec4(t, w);
    }
}

} // namespace vivid::render3d
