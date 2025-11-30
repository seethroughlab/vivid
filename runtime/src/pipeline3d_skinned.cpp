#include "pipeline3d_skinned.h"
#include <iostream>
#include <cstring>

namespace vivid {

// Built-in skinned mesh shader
namespace shadersSkinned {

const char* SKINNED_UNLIT = R"(
// Camera uniform - binding 0, group 0
struct CameraUniform {
    view: mat4x4f,
    projection: mat4x4f,
    viewProjection: mat4x4f,
    cameraPosition: vec3f,
    _pad: f32,
}

// Transform uniform - binding 0, group 1
struct TransformUniform {
    model: mat4x4f,
    normalMatrix: mat4x4f,
}

// Bone matrices - binding 0, group 2
const MAX_BONES: u32 = 128u;

struct BoneUniform {
    bones: array<mat4x4f, 128>,
}

@group(0) @binding(0) var<uniform> camera: CameraUniform;
@group(1) @binding(0) var<uniform> transform: TransformUniform;
@group(2) @binding(0) var<uniform> boneData: BoneUniform;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) tangent: vec4f,
    @location(4) boneIds: vec4i,
    @location(5) boneWeights: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldNormal: vec3f,
    @location(1) uv: vec2f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    // Apply bone transforms (GPU skinning)
    var skinnedPos = vec4f(0.0);
    var skinnedNormal = vec3f(0.0);

    // Accumulate bone influences
    for (var i = 0u; i < 4u; i = i + 1u) {
        let boneId = in.boneIds[i];
        let weight = in.boneWeights[i];

        if (weight > 0.0 && boneId >= 0 && boneId < 128) {
            let boneMatrix = boneData.bones[boneId];
            skinnedPos = skinnedPos + weight * (boneMatrix * vec4f(in.position, 1.0));
            skinnedNormal = skinnedNormal + weight * (mat3x3f(
                boneMatrix[0].xyz,
                boneMatrix[1].xyz,
                boneMatrix[2].xyz
            ) * in.normal);
        }
    }

    // Fallback if no bones affect this vertex
    let totalWeight = in.boneWeights.x + in.boneWeights.y + in.boneWeights.z + in.boneWeights.w;
    if (totalWeight < 0.001) {
        skinnedPos = vec4f(in.position, 1.0);
        skinnedNormal = in.normal;
    }

    // Apply model transform
    let worldPos = transform.model * skinnedPos;
    out.position = camera.viewProjection * worldPos;

    // Transform normal to world space
    out.worldNormal = normalize((transform.normalMatrix * vec4f(skinnedNormal, 0.0)).xyz);
    out.uv = in.uv;

    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Simple normal-based shading
    let normalColor = in.worldNormal * 0.5 + 0.5;
    return vec4f(normalColor, 1.0);
}
)";

} // namespace shadersSkinned

// Pipeline3DSkinnedInternal implementation

Pipeline3DSkinnedInternal::~Pipeline3DSkinnedInternal() {
    destroy();
}

Pipeline3DSkinnedInternal::Pipeline3DSkinnedInternal(Pipeline3DSkinnedInternal&& other) noexcept {
    *this = std::move(other);
}

Pipeline3DSkinnedInternal& Pipeline3DSkinnedInternal::operator=(Pipeline3DSkinnedInternal&& other) noexcept {
    if (this != &other) {
        destroy();
        pipeline_ = other.pipeline_;
        cameraBindGroupLayout_ = other.cameraBindGroupLayout_;
        transformBindGroupLayout_ = other.transformBindGroupLayout_;
        boneBindGroupLayout_ = other.boneBindGroupLayout_;
        pipelineLayout_ = other.pipelineLayout_;
        shaderModule_ = other.shaderModule_;
        device_ = other.device_;
        lastError_ = std::move(other.lastError_);

        other.pipeline_ = nullptr;
        other.cameraBindGroupLayout_ = nullptr;
        other.transformBindGroupLayout_ = nullptr;
        other.boneBindGroupLayout_ = nullptr;
        other.pipelineLayout_ = nullptr;
        other.shaderModule_ = nullptr;
        other.device_ = nullptr;
    }
    return *this;
}

void Pipeline3DSkinnedInternal::destroy() {
    if (pipeline_) wgpuRenderPipelineRelease(pipeline_);
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
    if (cameraBindGroupLayout_) wgpuBindGroupLayoutRelease(cameraBindGroupLayout_);
    if (transformBindGroupLayout_) wgpuBindGroupLayoutRelease(transformBindGroupLayout_);
    if (boneBindGroupLayout_) wgpuBindGroupLayoutRelease(boneBindGroupLayout_);
    if (shaderModule_) wgpuShaderModuleRelease(shaderModule_);

    pipeline_ = nullptr;
    pipelineLayout_ = nullptr;
    cameraBindGroupLayout_ = nullptr;
    transformBindGroupLayout_ = nullptr;
    boneBindGroupLayout_ = nullptr;
    shaderModule_ = nullptr;
    device_ = nullptr;
}

bool Pipeline3DSkinnedInternal::create(Renderer& renderer) {
    return create(renderer, shadersSkinned::SKINNED_UNLIT);
}

bool Pipeline3DSkinnedInternal::create(Renderer& renderer, const std::string& wgslSource) {
    destroy();
    device_ = renderer.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{.data = wgslSource.c_str(), .length = wgslSource.size()};

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    shaderModule_ = wgpuDeviceCreateShaderModule(device_, &moduleDesc);
    if (!shaderModule_) {
        lastError_ = "Failed to create shader module";
        return false;
    }

    // Camera bind group layout (group 0)
    WGPUBindGroupLayoutEntry cameraEntry = {};
    cameraEntry.binding = 0;
    cameraEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    cameraEntry.buffer.type = WGPUBufferBindingType_Uniform;
    cameraEntry.buffer.minBindingSize = sizeof(float) * 52;  // CameraUniform size

    WGPUBindGroupLayoutDescriptor cameraLayoutDesc = {};
    cameraLayoutDesc.entryCount = 1;
    cameraLayoutDesc.entries = &cameraEntry;
    cameraBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &cameraLayoutDesc);

    // Transform bind group layout (group 1)
    WGPUBindGroupLayoutEntry transformEntry = {};
    transformEntry.binding = 0;
    transformEntry.visibility = WGPUShaderStage_Vertex;
    transformEntry.buffer.type = WGPUBufferBindingType_Uniform;
    transformEntry.buffer.minBindingSize = sizeof(float) * 32;  // 2 mat4

    WGPUBindGroupLayoutDescriptor transformLayoutDesc = {};
    transformLayoutDesc.entryCount = 1;
    transformLayoutDesc.entries = &transformEntry;
    transformBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &transformLayoutDesc);

    // Bone bind group layout (group 2)
    WGPUBindGroupLayoutEntry boneEntry = {};
    boneEntry.binding = 0;
    boneEntry.visibility = WGPUShaderStage_Vertex;
    boneEntry.buffer.type = WGPUBufferBindingType_Uniform;
    boneEntry.buffer.minBindingSize = sizeof(BoneUniform);

    WGPUBindGroupLayoutDescriptor boneLayoutDesc = {};
    boneLayoutDesc.entryCount = 1;
    boneLayoutDesc.entries = &boneEntry;
    boneBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &boneLayoutDesc);

    // Pipeline layout with 3 bind groups
    WGPUBindGroupLayout layouts[] = {cameraBindGroupLayout_, transformBindGroupLayout_, boneBindGroupLayout_};
    WGPUPipelineLayoutDescriptor layoutDesc = {};
    layoutDesc.bindGroupLayoutCount = 3;
    layoutDesc.bindGroupLayouts = layouts;
    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &layoutDesc);

    // Vertex buffer layout for SkinnedVertex3D
    WGPUVertexAttribute attributes[6] = {};
    // Position (vec3f)
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = offsetof(SkinnedVertex3D, position);
    attributes[0].shaderLocation = 0;
    // Normal (vec3f)
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = offsetof(SkinnedVertex3D, normal);
    attributes[1].shaderLocation = 1;
    // UV (vec2f)
    attributes[2].format = WGPUVertexFormat_Float32x2;
    attributes[2].offset = offsetof(SkinnedVertex3D, uv);
    attributes[2].shaderLocation = 2;
    // Tangent (vec4f)
    attributes[3].format = WGPUVertexFormat_Float32x4;
    attributes[3].offset = offsetof(SkinnedVertex3D, tangent);
    attributes[3].shaderLocation = 3;
    // Bone IDs (ivec4 = 4 ints)
    attributes[4].format = WGPUVertexFormat_Sint32x4;
    attributes[4].offset = offsetof(SkinnedVertex3D, boneIds);
    attributes[4].shaderLocation = 4;
    // Bone Weights (vec4f)
    attributes[5].format = WGPUVertexFormat_Float32x4;
    attributes[5].offset = offsetof(SkinnedVertex3D, boneWeights);
    attributes[5].shaderLocation = 5;

    WGPUVertexBufferLayout vertexBufferLayout = {};
    vertexBufferLayout.arrayStride = sizeof(SkinnedVertex3D);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = 6;
    vertexBufferLayout.attributes = attributes;

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout_;

    // Vertex state
    pipelineDesc.vertex.module = shaderModule_;
    pipelineDesc.vertex.entryPoint = WGPUStringView{.data = "vs_main", .length = 7};
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;

    // Primitive state (no culling - FBX axis swaps can invert winding)
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    // Depth stencil
    WGPUDepthStencilState depthStencil = {};
    depthStencil.format = WGPUTextureFormat_Depth24Plus;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
    depthStencil.depthCompare = WGPUCompareFunction_Less;
    pipelineDesc.depthStencil = &depthStencil;

    // Multisample
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    // Fragment state
    WGPUBlendState blend = {};
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_Zero;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.blend = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = {};
    fragment.module = shaderModule_;
    fragment.entryPoint = WGPUStringView{.data = "fs_main", .length = 7};
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    pipelineDesc.fragment = &fragment;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    if (!pipeline_) {
        lastError_ = "Failed to create render pipeline";
        return false;
    }

    std::cout << "[Pipeline3DSkinned] Created successfully\n";
    return true;
}

// SkinnedMeshRenderer implementation

SkinnedMeshRenderer::~SkinnedMeshRenderer() {
    for (auto buffer : boneBuffers_) {
        if (buffer) wgpuBufferRelease(buffer);
    }
}

void SkinnedMeshRenderer::init(Renderer& renderer) {
    renderer_ = &renderer;
}

SkinnedMeshGPU SkinnedMeshRenderer::createMesh(const std::vector<SkinnedVertex3D>& vertices,
                                                 const std::vector<uint32_t>& indices) {
    SkinnedMeshGPU mesh;
    if (!renderer_ || vertices.empty() || indices.empty()) return mesh;

    WGPUDevice device = renderer_->device();

    // Create vertex buffer
    WGPUBufferDescriptor vbDesc = {};
    vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    vbDesc.size = vertices.size() * sizeof(SkinnedVertex3D);
    mesh.vertexBuffer = wgpuDeviceCreateBuffer(device, &vbDesc);
    wgpuQueueWriteBuffer(renderer_->queue(), mesh.vertexBuffer, 0,
                          vertices.data(), vbDesc.size);

    // Create index buffer
    WGPUBufferDescriptor ibDesc = {};
    ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    ibDesc.size = indices.size() * sizeof(uint32_t);
    mesh.indexBuffer = wgpuDeviceCreateBuffer(device, &ibDesc);
    wgpuQueueWriteBuffer(renderer_->queue(), mesh.indexBuffer, 0,
                          indices.data(), ibDesc.size);

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.indexCount = static_cast<uint32_t>(indices.size());

    return mesh;
}

void SkinnedMeshRenderer::destroyMesh(SkinnedMeshGPU& mesh) {
    if (mesh.vertexBuffer) wgpuBufferRelease(mesh.vertexBuffer);
    if (mesh.indexBuffer) wgpuBufferRelease(mesh.indexBuffer);
    mesh = {};
}

WGPUBindGroup SkinnedMeshRenderer::createBoneBindGroup(WGPUBindGroupLayout layout,
                                                         const std::vector<glm::mat4>& boneMatrices) {
    if (!renderer_) return nullptr;

    WGPUDevice device = renderer_->device();

    // Create bone uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    bufDesc.size = sizeof(BoneUniform);
    WGPUBuffer boneBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    boneBuffers_.push_back(boneBuffer);

    // Initialize with identity matrices
    BoneUniform uniform;
    for (int i = 0; i < MAX_BONES; ++i) {
        uniform.bones[i] = glm::mat4(1.0f);
    }

    // Copy in actual bone matrices
    size_t count = std::min(boneMatrices.size(), static_cast<size_t>(MAX_BONES));
    for (size_t i = 0; i < count; ++i) {
        uniform.bones[i] = boneMatrices[i];
    }

    wgpuQueueWriteBuffer(renderer_->queue(), boneBuffer, 0, &uniform, sizeof(uniform));

    // Create bind group
    WGPUBindGroupEntry entry = {};
    entry.binding = 0;
    entry.buffer = boneBuffer;
    entry.offset = 0;
    entry.size = sizeof(BoneUniform);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = layout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &entry;

    return wgpuDeviceCreateBindGroup(device, &bgDesc);
}

void SkinnedMeshRenderer::updateBoneMatrices(WGPUBuffer boneBuffer,
                                               const std::vector<glm::mat4>& boneMatrices) {
    if (!renderer_ || !boneBuffer) return;

    BoneUniform uniform;
    for (int i = 0; i < MAX_BONES; ++i) {
        uniform.bones[i] = glm::mat4(1.0f);
    }

    size_t count = std::min(boneMatrices.size(), static_cast<size_t>(MAX_BONES));
    for (size_t i = 0; i < count; ++i) {
        uniform.bones[i] = boneMatrices[i];
    }

    wgpuQueueWriteBuffer(renderer_->queue(), boneBuffer, 0, &uniform, sizeof(uniform));
}

void SkinnedMeshRenderer::releaseBindGroup(WGPUBindGroup bindGroup) {
    if (bindGroup) wgpuBindGroupRelease(bindGroup);
}

} // namespace vivid
