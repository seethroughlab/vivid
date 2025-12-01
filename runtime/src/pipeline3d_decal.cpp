#include "pipeline3d_decal.h"
#include <cstring>
#include <iostream>

namespace vivid {

// ============================================================================
// Decal projection shader
// ============================================================================

namespace shaders3d {

const char* DECAL_PROJECTION = R"(
// Decal uniform buffer
struct DecalUniform {
    decalMatrix: mat4x4<f32>,    // World-to-decal space transform
    invViewProj: mat4x4<f32>,    // Inverse view-projection
    color: vec4<f32>,            // Decal color and opacity
    depthBias: f32,              // Z-bias
    blendMode: i32,              // 0=Normal, 1=Multiply, 2=Additive, 3=Overlay
    wrapU: i32,                  // Wrap in U direction
    wrapV: i32,                  // Wrap in V direction
};

@group(0) @binding(0) var<uniform> decal: DecalUniform;

@group(1) @binding(0) var depthTexture: texture_depth_2d;
@group(1) @binding(1) var decalTexture: texture_2d<f32>;
@group(1) @binding(2) var texSampler: sampler;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

// Full-screen quad vertex shader
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    // Generate full-screen triangle positions
    var positions = array<vec2<f32>, 6>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 1.0, -1.0),
        vec2<f32>( 1.0,  1.0),
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 1.0,  1.0),
        vec2<f32>(-1.0,  1.0)
    );

    var uvs = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 1.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(0.0, 1.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(0.0, 0.0)
    );

    var out: VertexOutput;
    out.position = vec4<f32>(positions[vertexIndex], 0.0, 1.0);
    out.uv = uvs[vertexIndex];
    return out;
}

// Reconstruct world position from depth
fn reconstructWorldPosition(uv: vec2<f32>, depth: f32) -> vec3<f32> {
    // Convert UV to clip space (-1 to 1)
    let clipX = uv.x * 2.0 - 1.0;
    let clipY = (1.0 - uv.y) * 2.0 - 1.0;  // Flip Y

    // Clip space position
    let clipPos = vec4<f32>(clipX, clipY, depth, 1.0);

    // Transform to world space
    let worldPos = decal.invViewProj * clipPos;
    return worldPos.xyz / worldPos.w;
}

// Overlay blend mode
fn overlay(base: f32, blend: f32) -> f32 {
    if (base < 0.5) {
        return 2.0 * base * blend;
    } else {
        return 1.0 - 2.0 * (1.0 - base) * (1.0 - blend);
    }
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    // Sample depth
    let depth = textureLoad(depthTexture, vec2<i32>(in.position.xy), 0);

    // Skip sky/background (depth = 1.0)
    if (depth >= 1.0) {
        discard;
    }

    // Reconstruct world position
    let worldPos = reconstructWorldPosition(in.uv, depth);

    // Transform to decal space
    let decalPos4 = decal.decalMatrix * vec4<f32>(worldPos, 1.0);
    let decalPos = decalPos4.xyz;

    // Check if within decal box [-0.5, 0.5]
    let absPos = abs(decalPos);
    if (absPos.x > 0.5 || absPos.y > 0.5 || absPos.z > 0.5 + decal.depthBias) {
        discard;
    }

    // Calculate UV from decal position (XY maps to UV)
    var decalUV = decalPos.xy + vec2<f32>(0.5);

    // Handle wrapping
    if (decal.wrapU == 0 && (decalUV.x < 0.0 || decalUV.x > 1.0)) {
        discard;
    }
    if (decal.wrapV == 0 && (decalUV.y < 0.0 || decalUV.y > 1.0)) {
        discard;
    }

    // Wrap UVs if enabled
    if (decal.wrapU != 0) {
        decalUV.x = fract(decalUV.x);
    }
    if (decal.wrapV != 0) {
        decalUV.y = fract(decalUV.y);
    }

    // Sample decal texture
    let decalColor = textureSample(decalTexture, texSampler, decalUV) * decal.color;

    // Return color with alpha for blending
    // The actual blend mode is handled by different pipeline states
    return decalColor;
}
)";

} // namespace shaders3d

// ============================================================================
// Pipeline implementation
// ============================================================================

Pipeline3DDecal::~Pipeline3DDecal() {
    destroy();
}

bool Pipeline3DDecal::init(Renderer& renderer) {
    renderer_ = &renderer;
    return createPipelines();
}

void Pipeline3DDecal::destroy() {
    if (quadIndexBuffer_) {
        wgpuBufferRelease(quadIndexBuffer_);
        quadIndexBuffer_ = nullptr;
    }
    if (quadVertexBuffer_) {
        wgpuBufferRelease(quadVertexBuffer_);
        quadVertexBuffer_ = nullptr;
    }
    if (decalBuffer_) {
        wgpuBufferRelease(decalBuffer_);
        decalBuffer_ = nullptr;
    }
    if (depthSampler_) {
        wgpuSamplerRelease(depthSampler_);
        depthSampler_ = nullptr;
    }
    if (sampler_) {
        wgpuSamplerRelease(sampler_);
        sampler_ = nullptr;
    }
    for (int i = 0; i < BLEND_MODE_COUNT; ++i) {
        if (pipelines_[i]) {
            wgpuRenderPipelineRelease(pipelines_[i]);
            pipelines_[i] = nullptr;
        }
    }
    if (pipelineLayout_) {
        wgpuPipelineLayoutRelease(pipelineLayout_);
        pipelineLayout_ = nullptr;
    }
    if (textureLayout_) {
        wgpuBindGroupLayoutRelease(textureLayout_);
        textureLayout_ = nullptr;
    }
    if (decalUniformLayout_) {
        wgpuBindGroupLayoutRelease(decalUniformLayout_);
        decalUniformLayout_ = nullptr;
    }
    if (shaderModule_) {
        wgpuShaderModuleRelease(shaderModule_);
        shaderModule_ = nullptr;
    }
    renderer_ = nullptr;
}

WGPURenderPipeline Pipeline3DDecal::createBlendPipeline(DecalBlendMode mode) {
    WGPUDevice device = renderer_->device();

    // Create blend state based on mode
    WGPUBlendState blendState = {};
    blendState.alpha.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

    switch (mode) {
        case DecalBlendMode::Normal:
            // Standard alpha blending: src * alpha + dst * (1 - alpha)
            blendState.color.operation = WGPUBlendOperation_Add;
            blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
            blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            break;

        case DecalBlendMode::Multiply:
            // Multiply: src * dst (approximated with blend)
            // Result = dst * src.rgb * src.a + dst * (1 - src.a) = dst * lerp(1, src.rgb, src.a)
            blendState.color.operation = WGPUBlendOperation_Add;
            blendState.color.srcFactor = WGPUBlendFactor_Dst;
            blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            break;

        case DecalBlendMode::Additive:
            // Additive: src * alpha + dst
            blendState.color.operation = WGPUBlendOperation_Add;
            blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
            blendState.color.dstFactor = WGPUBlendFactor_One;
            break;

        case DecalBlendMode::Overlay:
            // Overlay is complex - use soft light approximation (additive with clamping)
            // For hardware blend, we approximate with screen blend
            blendState.color.operation = WGPUBlendOperation_Add;
            blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
            blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrc;
            break;
    }

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule_;
    fragmentState.entryPoint = WGPUStringView{.data = "fs_main", .length = 7};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout_;

    // Vertex state (no vertex buffers - full-screen quad from vertex index)
    pipelineDesc.vertex.module = shaderModule_;
    pipelineDesc.vertex.entryPoint = WGPUStringView{.data = "vs_main", .length = 7};

    // Primitive state
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    // No depth testing for decals (we handle it in shader)
    pipelineDesc.depthStencil = nullptr;

    // Multisample state
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    pipelineDesc.fragment = &fragmentState;

    return wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
}

WGPURenderPipeline Pipeline3DDecal::getPipelineForBlendMode(DecalBlendMode mode) {
    int index = static_cast<int>(mode);
    if (index < 0 || index >= BLEND_MODE_COUNT) {
        index = 0;  // Default to normal
    }

    // Lazy creation
    if (!pipelines_[index]) {
        pipelines_[index] = createBlendPipeline(static_cast<DecalBlendMode>(index));
    }

    return pipelines_[index];
}

bool Pipeline3DDecal::createPipelines() {
    WGPUDevice device = renderer_->device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{.data = shaders3d::DECAL_PROJECTION, .length = strlen(shaders3d::DECAL_PROJECTION)};

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);

    shaderModule_ = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule_) {
        std::cerr << "Pipeline3DDecal: Failed to create shader module" << std::endl;
        return false;
    }

    // Create bind group layout for decal uniform (group 0)
    WGPUBindGroupLayoutEntry uniformEntry = {};
    uniformEntry.binding = 0;
    uniformEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    uniformEntry.buffer.type = WGPUBufferBindingType_Uniform;
    uniformEntry.buffer.minBindingSize = sizeof(DecalUniform);

    WGPUBindGroupLayoutDescriptor uniformLayoutDesc = {};
    uniformLayoutDesc.entryCount = 1;
    uniformLayoutDesc.entries = &uniformEntry;
    decalUniformLayout_ = wgpuDeviceCreateBindGroupLayout(device, &uniformLayoutDesc);

    // Create bind group layout for textures (group 1)
    WGPUBindGroupLayoutEntry textureEntries[3] = {};

    // Depth texture
    textureEntries[0].binding = 0;
    textureEntries[0].visibility = WGPUShaderStage_Fragment;
    textureEntries[0].texture.sampleType = WGPUTextureSampleType_Depth;
    textureEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Decal texture
    textureEntries[1].binding = 1;
    textureEntries[1].visibility = WGPUShaderStage_Fragment;
    textureEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
    textureEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Sampler
    textureEntries[2].binding = 2;
    textureEntries[2].visibility = WGPUShaderStage_Fragment;
    textureEntries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor textureLayoutDesc = {};
    textureLayoutDesc.entryCount = 3;
    textureLayoutDesc.entries = textureEntries;
    textureLayout_ = wgpuDeviceCreateBindGroupLayout(device, &textureLayoutDesc);

    // Create pipeline layout
    WGPUBindGroupLayout layouts[] = { decalUniformLayout_, textureLayout_ };
    WGPUPipelineLayoutDescriptor layoutDesc = {};
    layoutDesc.bindGroupLayoutCount = 2;
    layoutDesc.bindGroupLayouts = layouts;
    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

    // Create the default (Normal) pipeline eagerly
    pipelines_[0] = createBlendPipeline(DecalBlendMode::Normal);
    if (!pipelines_[0]) {
        std::cerr << "Pipeline3DDecal: Failed to create render pipeline" << std::endl;
        return false;
    }

    // Create sampler for decal texture
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(DecalUniform);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    decalBuffer_ = wgpuDeviceCreateBuffer(device, &bufferDesc);

    return true;
}

void Pipeline3DDecal::renderDecal(const Decal& decal, const Camera3D& camera,
                                   const Texture& depthTexture, Texture& colorOutput) {
    if (!valid() || !decal.texture || !decal.texture->valid()) {
        return;
    }

    WGPUDevice device = renderer_->device();
    WGPUQueue queue = renderer_->queue();

    // Get depth texture view
    auto* depthData = getTextureData(depthTexture);
    if (!depthData || !depthData->texture) {
        std::cerr << "Pipeline3DDecal: Invalid depth texture" << std::endl;
        return;
    }

    // Use the pre-created depth sample view from TextureData
    // (Renderer creates this with Depth24Plus format and DepthOnly aspect for shader sampling)
    WGPUTextureView depthView = depthData->view;
    if (!depthView) {
        std::cerr << "Pipeline3DDecal: Invalid depth texture view" << std::endl;
        return;
    }

    // Calculate inverse view-projection matrix
    float aspectRatio = static_cast<float>(colorOutput.width) / colorOutput.height;
    glm::mat4 viewProj = camera.viewProjectionMatrix(aspectRatio);
    glm::mat4 invViewProj = glm::inverse(viewProj);

    // Update decal uniform
    DecalUniform uniform = makeDecalUniform(decal, invViewProj);
    wgpuQueueWriteBuffer(queue, decalBuffer_, 0, &uniform, sizeof(DecalUniform));

    // Create decal uniform bind group
    WGPUBindGroupEntry uniformEntry = {};
    uniformEntry.binding = 0;
    uniformEntry.buffer = decalBuffer_;
    uniformEntry.size = sizeof(DecalUniform);

    WGPUBindGroupDescriptor uniformGroupDesc = {};
    uniformGroupDesc.layout = decalUniformLayout_;
    uniformGroupDesc.entryCount = 1;
    uniformGroupDesc.entries = &uniformEntry;
    WGPUBindGroup uniformGroup = wgpuDeviceCreateBindGroup(device, &uniformGroupDesc);

    // Create texture bind group
    auto* decalData = getTextureData(*decal.texture);

    WGPUBindGroupEntry textureEntries[3] = {};
    textureEntries[0].binding = 0;
    textureEntries[0].textureView = depthView;

    textureEntries[1].binding = 1;
    textureEntries[1].textureView = decalData->view;

    textureEntries[2].binding = 2;
    textureEntries[2].sampler = sampler_;

    WGPUBindGroupDescriptor textureGroupDesc = {};
    textureGroupDesc.layout = textureLayout_;
    textureGroupDesc.entryCount = 3;
    textureGroupDesc.entries = textureEntries;
    WGPUBindGroup textureGroup = wgpuDeviceCreateBindGroup(device, &textureGroupDesc);

    // Create command encoder
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

    // Set up render pass
    auto* outputData = getTextureData(colorOutput);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = outputData->view;
    colorAttachment.loadOp = WGPULoadOp_Load;  // Preserve existing color
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    // Draw decal with appropriate blend mode pipeline
    WGPURenderPipeline pipeline = getPipelineForBlendMode(decal.blendMode);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, uniformGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, textureGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);  // 6 vertices for full-screen quad

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Submit
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(queue, 1, &commands);

    // Cleanup
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(textureGroup);
    wgpuBindGroupRelease(uniformGroup);
    // Note: depthView is shared from Renderer, don't release it here
}

void Pipeline3DDecal::renderDecals(const std::vector<Decal>& decals, const Camera3D& camera,
                                    const Texture& depthTexture, Texture& colorOutput) {
    // For now, render each decal individually
    // This could be optimized with batching in the future
    for (const auto& decal : decals) {
        renderDecal(decal, camera, depthTexture, colorOutput);
    }
}

} // namespace vivid
