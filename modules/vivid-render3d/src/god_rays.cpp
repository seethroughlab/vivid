#include <vivid/render3d/god_rays.h>
#include <vivid/render3d/camera.h>
#include <vivid/render3d/camera_operator.h>
#include <vivid/render3d/light_operators.h>
#include <vivid/context.h>
#include <glm/gtc/matrix_transform.hpp>

using vivid::effects::toStringView;

namespace vivid::render3d {

static const char* GOD_RAYS_SHADER = R"(
struct Uniforms {
    lightScreenPos: vec2f,      // Light position in screen UV space (offset 0)
    exposure: f32,              // offset 8
    decay: f32,                 // offset 12
    density: f32,               // offset 16
    weight: f32,                // offset 20
    samples: i32,               // offset 24
    threshold: f32,             // offset 28
    blend: f32,                 // offset 32
    _pad0: f32,                 // offset 36 (padding to 64 bytes)
    _pad1: f32,                 // offset 40
    _pad2: f32,                 // offset 44
    _pad3: f32,                 // offset 48
    _pad4: f32,                 // offset 52
    _pad5: f32,                 // offset 56
    _pad6: f32,                 // offset 60
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var inputSampler: sampler;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var pos = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f(3.0, -1.0),
        vec2f(-1.0, 3.0)
    );
    var output: VertexOutput;
    output.position = vec4f(pos[vertexIndex], 0.0, 1.0);
    output.uv = pos[vertexIndex] * 0.5 + 0.5;
    output.uv.y = 1.0 - output.uv.y;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let sceneColor = textureSample(inputTex, inputSampler, input.uv);

    // Direction from pixel to light (in UV space)
    let deltaUV = (input.uv - uniforms.lightScreenPos) * uniforms.density / f32(uniforms.samples);

    var uv = input.uv;
    var illumination = vec3f(0.0);
    var currentWeight = uniforms.weight;
    var currentDecay = 1.0;

    // Sample along the ray toward the light
    for (var i = 0; i < uniforms.samples; i = i + 1) {
        uv -= deltaUV;

        // Clamp to valid UV range
        let clampedUV = clamp(uv, vec2f(0.0), vec2f(1.0));

        // Sample the scene
        let sampleColor = textureSample(inputTex, inputSampler, clampedUV);

        // Only accumulate bright pixels (above threshold)
        let brightness = max(sampleColor.r, max(sampleColor.g, sampleColor.b));
        let contribution = select(vec3f(0.0), sampleColor.rgb, brightness > uniforms.threshold);

        // Accumulate with decay
        illumination += contribution * currentDecay * currentWeight;
        currentDecay *= uniforms.decay;
    }

    // Apply exposure
    illumination *= uniforms.exposure;

    // DEBUG: Show only the illumination to see what's being accumulated
    // return vec4f(illumination * 10.0, 1.0);  // Uncomment to debug

    // Blend with original scene (additive)
    let finalColor = sceneColor.rgb + illumination * uniforms.blend;

    return vec4f(finalColor, sceneColor.a);
}
)";

GodRays::GodRays() = default;
GodRays::~GodRays() { cleanup(); }

void GodRays::init(Context& ctx) {
    vivid::effects::TextureOperator::init(ctx);
}

void GodRays::createPipeline(Context& ctx) {
    auto device = ctx.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(GOD_RAYS_SHADER);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[3] = {};

    // Uniforms
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].buffer.minBindingSize = 64;

    // Input texture
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Sampler
    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 3;
    layoutDesc.entries = layoutEntries;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = 64;
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

    // Create render pipeline (must match TextureOperator output format)
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA16Float;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    m_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    m_initialized = true;
}

void GodRays::process(Context& ctx) {
    if (!m_input) return;

    auto device = ctx.device();
    auto queue = ctx.queue();

    // Create pipeline on first use
    if (!m_initialized) {
        createPipeline(ctx);
    }

    // Get input texture
    WGPUTextureView inputView = m_input->outputView();
    if (!inputView) return;

    // Get light screen position
    glm::vec2 lightScreenPos(0.5f, 0.3f);  // Default to upper-center

    if (m_camera) {
        glm::vec3 lightPos = m_lightPos;
        if (m_useLightInput && m_light) {
            lightPos = m_light->outputLight().position;
        }

        // Project light position to screen space
        const auto& cam = m_camera->outputCamera();
        glm::mat4 viewProj = cam.projectionMatrix() * cam.viewMatrix();
        glm::vec4 clipPos = viewProj * glm::vec4(lightPos, 1.0f);

        if (clipPos.w > 0.0f) {
            glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
            lightScreenPos.x = ndc.x * 0.5f + 0.5f;
            lightScreenPos.y = -ndc.y * 0.5f + 0.5f;  // Flip Y for UV space
        }
    }

    // Update uniforms (must match shader struct layout)
    struct Uniforms {
        float lightScreenPosX, lightScreenPosY;  // offset 0, 4
        float exposure;                           // offset 8
        float decay;                              // offset 12
        float density;                            // offset 16
        float weight;                             // offset 20
        int32_t samples;                          // offset 24
        float threshold;                          // offset 28
        float blend;                              // offset 32
        float _pad[7];                            // offset 36-60, padding to 64 bytes
    } uniforms;

    uniforms.lightScreenPosX = lightScreenPos.x;
    uniforms.lightScreenPosY = lightScreenPos.y;
    uniforms.exposure = static_cast<float>(exposure);
    uniforms.decay = static_cast<float>(decay);
    uniforms.density = static_cast<float>(density);
    uniforms.weight = static_cast<float>(weight);
    uniforms.samples = static_cast<int32_t>(samples);
    uniforms.threshold = static_cast<float>(threshold);
    uniforms.blend = static_cast<float>(blend);

    wgpuQueueWriteBuffer(queue, m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Get input dimensions and match output size
    auto* texInput = dynamic_cast<vivid::effects::TextureOperator*>(m_input);
    if (!texInput) return;

    int inputWidth = texInput->outputWidth();
    int inputHeight = texInput->outputHeight();

    // Skip if input not ready yet (first frame issue)
    if (inputWidth <= 0 || inputHeight <= 0) return;

    // Recreate output texture if size changed
    if (m_width != inputWidth || m_height != inputHeight || !m_output) {
        m_width = inputWidth;
        m_height = inputHeight;
        createOutput(ctx, m_width, m_height);
    }

    // Create bind group with current input texture
    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].buffer = m_uniformBuffer;
    entries[0].size = 64;
    entries[1].binding = 1;
    entries[1].textureView = inputView;
    entries[2].binding = 2;
    entries[2].sampler = m_sampler;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = m_bindGroupLayout;
    bindGroupDesc.entryCount = 3;
    bindGroupDesc.entries = entries;

    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
    }
    m_bindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);

    // Create render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuRenderPassEncoderRelease(pass);
}

void GodRays::cleanup() {
    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
        m_bindGroup = nullptr;
    }
    if (m_sampler) {
        wgpuSamplerRelease(m_sampler);
        m_sampler = nullptr;
    }
    if (m_uniformBuffer) {
        wgpuBufferRelease(m_uniformBuffer);
        m_uniformBuffer = nullptr;
    }
    if (m_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    m_initialized = false;
    vivid::effects::TextureOperator::cleanup();
}

} // namespace vivid::render3d
