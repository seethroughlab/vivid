// Vivid Effects 2D - Plexus Implementation
// GPU-based particle network with proximity connections (2D/3D)

#include <vivid/effects/plexus.h>
#include <vivid/context.h>
#include <cmath>
#include <cstring>
#include <iostream>

namespace vivid::effects {

// Uniforms for line rendering (with 3D camera matrix)
struct PlexusLineUniforms {
    float viewProj[16];  // 4x4 view-projection matrix
    float resolution[2];
    float lineWidth;
    float _pad1;
    float color[4];
};

// Uniforms for node rendering
struct PlexusNodeUniforms {
    float viewProj[16];  // 4x4 view-projection matrix
    float resolution[2];
    float aspectRatio;
    float _pad;
};

// Line vertex (quad corner)
struct LineVertex {
    float x, y;
    float along;  // 0 at start, 1 at end
    float across; // -1 to 1 perpendicular to line
};

// Node vertex (local circle position)
struct NodeVertex {
    float x, y;
};

// WGSL shader for 3D line rendering
static const char* LINE_SHADER = R"(
struct Uniforms {
    viewProj: mat4x4f,
    resolution: vec2f,
    lineWidth: f32,
    _pad: f32,
    color: vec4f,
}

struct VertexInput {
    @location(0) localPos: vec2f,
    @location(1) alongAcross: vec2f,
}

struct InstanceInput {
    @location(2) start: vec4f,   // xyz + pad
    @location(3) endAlpha: vec4f, // xyz + alpha
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) alpha: f32,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(vert: VertexInput, inst: InstanceInput) -> VertexOutput {
    var output: VertexOutput;

    let start3d = inst.start.xyz;
    let end3d = inst.endAlpha.xyz;
    let lineAlpha = inst.endAlpha.w;

    // Project start and end points
    let startClip = uniforms.viewProj * vec4f(start3d, 1.0);
    let endClip = uniforms.viewProj * vec4f(end3d, 1.0);

    // Convert to NDC
    let startNdc = startClip.xy / startClip.w;
    let endNdc = endClip.xy / endClip.w;

    // Direction in screen space
    let dir = endNdc - startNdc;
    let len = length(dir);
    let tangent = select(vec2f(1.0, 0.0), dir / len, len > 0.0001);
    let normal = vec2f(-tangent.y, tangent.x);

    // Interpolate along line
    let t = vert.alongAcross.x;
    let clipPos = mix(startClip, endClip, t);

    // Offset perpendicular to line for width (in screen space)
    let halfWidth = uniforms.lineWidth / uniforms.resolution.y;
    let offset = normal * vert.alongAcross.y * halfWidth * clipPos.w;

    output.position = vec4f(clipPos.xy + offset, clipPos.z, clipPos.w);
    output.alpha = lineAlpha;

    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return vec4f(uniforms.color.rgb, uniforms.color.a * input.alpha);
}
)";

// WGSL shader for 3D node rendering (billboards)
static const char* NODE_SHADER = R"(
struct Uniforms {
    viewProj: mat4x4f,
    resolution: vec2f,
    aspectRatio: f32,
    _pad: f32,
}

struct VertexInput {
    @location(0) localPos: vec2f,
}

struct InstanceInput {
    @location(1) posSize: vec4f,  // xyz + size
    @location(2) color: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) localPos: vec2f,
    @location(1) color: vec4f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(vert: VertexInput, inst: InstanceInput) -> VertexOutput {
    var output: VertexOutput;

    let worldPos = inst.posSize.xyz;
    let size = inst.posSize.w;

    // Project center to clip space
    let clipPos = uniforms.viewProj * vec4f(worldPos, 1.0);

    // Billboard offset in screen space (size scales with distance for consistency)
    var offset = vert.localPos * size;
    offset.x /= uniforms.aspectRatio;

    output.position = vec4f(clipPos.xy + offset * clipPos.w, clipPos.z, clipPos.w);
    output.localPos = vert.localPos;
    output.color = inst.color;

    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let dist = length(input.localPos);
    let edge = smoothstep(1.0, 0.9, dist);
    return vec4f(input.color.rgb, input.color.a * edge);
}
)";

// Simple hash for noise
static float hash(float n) {
    return std::fmod(std::sin(n) * 43758.5453f, 1.0f);
}

static float noise2D(float x, float y) {
    float i = std::floor(x);
    float j = std::floor(y);
    float fx = x - i;
    float fy = y - j;

    float a = hash(i + j * 57.0f);
    float b = hash(i + 1.0f + j * 57.0f);
    float c = hash(i + (j + 1.0f) * 57.0f);
    float d = hash(i + 1.0f + (j + 1.0f) * 57.0f);

    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);

    return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
}

// Build view-projection matrix
static void buildViewProjection(float* out, float camDist, float angleY, float aspectRatio, bool is3D) {
    if (!is3D) {
        // 2D orthographic: particles centered at origin, scale to fill screen
        std::memset(out, 0, 16 * sizeof(float));
        out[0] = 2.0f;   // scale x (0.5 -> 1.0 in clip space)
        out[5] = -2.0f;  // scale y, flip for screen coords
        out[10] = 1.0f;  // z passthrough
        out[15] = 1.0f;  // w = 1
        return;
    }

    // 3D perspective with orbit camera
    float cosA = std::cos(angleY);
    float sinA = std::sin(angleY);

    // Camera position orbiting around origin
    float camX = sinA * camDist;
    float camZ = cosA * camDist;
    float camY = camDist * 0.3f;  // Slight elevation

    // View matrix (look at origin)
    float fx = -camX, fy = -camY, fz = -camZ;  // Forward (to origin)
    float fLen = std::sqrt(fx*fx + fy*fy + fz*fz);
    fx /= fLen; fy /= fLen; fz /= fLen;

    float ux = 0, uy = 1, uz = 0;  // Up
    // Right = forward x up
    float rx = fy * uz - fz * uy;
    float ry = fz * ux - fx * uz;
    float rz = fx * uy - fy * ux;
    // Recompute up = right x forward
    ux = ry * fz - rz * fy;
    uy = rz * fx - rx * fz;
    uz = rx * fy - ry * fx;

    float view[16] = {
        rx, ux, -fx, 0,
        ry, uy, -fy, 0,
        rz, uz, -fz, 0,
        -(rx*camX + ry*camY + rz*camZ),
        -(ux*camX + uy*camY + uz*camZ),
        (fx*camX + fy*camY + fz*camZ),
        1
    };

    // Perspective projection
    float fov = 0.8f;  // ~45 degrees
    float near = 0.1f, far = 100.0f;
    float f = 1.0f / std::tan(fov / 2.0f);
    float proj[16] = {
        f / aspectRatio, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (far + near) / (near - far), -1,
        0, 0, (2 * far * near) / (near - far), 0
    };

    // Multiply proj * view
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i * 4 + j] = 0;
            for (int k = 0; k < 4; k++) {
                out[i * 4 + j] += proj[i * 4 + k] * view[k * 4 + j];
            }
        }
    }
}

Plexus::Plexus() : m_rng(m_seed) {}

Plexus::~Plexus() {
    cleanup();
}

void Plexus::init(Context& ctx) {
    if (!beginInit()) return;

    createOutput(ctx);
    WGPUDevice device = ctx.device();
    createPipelines(device);
}

void Plexus::initNodes() {
    m_nodes.resize(m_nodeCount);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

    for (auto& node : m_nodes) {
        node.position.x = dist(m_rng) * m_spread;
        node.position.y = dist(m_rng) * m_spread;
        node.position.z = m_3dEnabled ? dist(m_rng) * m_depth : 0.0f;
        node.velocity = glm::vec3(0.0f);
    }
    m_nodesInitialized = true;
}

void Plexus::updateNodes(float dt, float time) {
    const float repulsionRadius = 0.15f;
    const float repulsionStrength = 0.12f;

    for (size_t i = 0; i < m_nodes.size(); i++) {
        auto& node = m_nodes[i];

        // Turbulence (noise-based force)
        float noiseX = noise2D(node.position.x * 3.0f + time * 0.5f,
                               node.position.y * 3.0f) * 2.0f - 1.0f;
        float noiseY = noise2D(node.position.x * 3.0f,
                               node.position.y * 3.0f + time * 0.5f) * 2.0f - 1.0f;
        node.velocity.x += noiseX * m_turbulence * dt;
        node.velocity.y += noiseY * m_turbulence * dt;

        if (m_3dEnabled) {
            float noiseZ = noise2D(node.position.y * 3.0f + time * 0.3f,
                                   node.position.z * 3.0f) * 2.0f - 1.0f;
            node.velocity.z += noiseZ * m_turbulence * dt * 0.5f;
        }

        // Inter-particle repulsion (2D or 3D based on mode)
        for (size_t j = 0; j < m_nodes.size(); j++) {
            if (i == j) continue;
            glm::vec3 diff = node.position - m_nodes[j].position;
            if (!m_3dEnabled) diff.z = 0.0f;
            float dist = glm::length(diff);
            if (dist < repulsionRadius && dist > 0.001f) {
                float force = repulsionStrength * (1.0f - dist / repulsionRadius);
                node.velocity += (diff / dist) * force * dt;
            }
        }

        // Soft center attraction - gets exponentially stronger further from center
        // No hard boundaries, just smooth force falloff
        glm::vec3 toCenter = -node.position;
        if (!m_3dEnabled) toCenter.z = 0.0f;
        float distFromCenter = glm::length(toCenter);

        // Exponential falloff: gentle near center, strong at edges
        float normalizedDist = distFromCenter / (m_spread * 0.5f);
        float attractionStrength = m_centerAttraction * (1.0f + normalizedDist * normalizedDist * 4.0f);
        if (distFromCenter > 0.001f) {
            node.velocity += glm::normalize(toCenter) * attractionStrength * dt;
        }

        // Drag
        node.velocity *= (1.0f - m_drag * dt);

        // Update position
        node.position += node.velocity * dt;

        // Keep Z flat in 2D mode
        if (!m_3dEnabled) {
            node.position.z = 0.0f;
            node.velocity.z = 0.0f;
        }
    }

    // Update camera angle for auto-rotate
    if (m_3dEnabled) {
        m_cameraAngle += m_autoRotateSpeed * dt;
    }
}

void Plexus::findConnections() {
    m_lines.clear();
    float maxDistSq = m_connectionDist * m_connectionDist;

    for (size_t i = 0; i < m_nodes.size(); i++) {
        for (size_t j = i + 1; j < m_nodes.size(); j++) {
            glm::vec3 diff = m_nodes[j].position - m_nodes[i].position;
            float distSq = glm::dot(diff, diff);

            if (distSq < maxDistSq && distSq > 0.0001f) {
                float dist = std::sqrt(distSq);
                float alpha = 1.0f - dist / m_connectionDist;

                LineInstance line;
                line.start = glm::vec4(m_nodes[i].position, 0.0f);
                line.end = glm::vec4(m_nodes[j].position, alpha);
                m_lines.push_back(line);
            }
        }
    }
}

void Plexus::createPipelines(WGPUDevice device) {
    // === LINE PIPELINE ===

    // Line uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(PlexusLineUniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_lineUniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);

    // Line bind group layout
    WGPUBindGroupLayoutEntry lineLayoutEntry = {};
    lineLayoutEntry.binding = 0;
    lineLayoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    lineLayoutEntry.buffer.type = WGPUBufferBindingType_Uniform;

    WGPUBindGroupLayoutDescriptor lineLayoutDesc = {};
    lineLayoutDesc.entryCount = 1;
    lineLayoutDesc.entries = &lineLayoutEntry;
    m_lineBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &lineLayoutDesc);

    // Line bind group
    WGPUBindGroupEntry lineBgEntry = {};
    lineBgEntry.binding = 0;
    lineBgEntry.buffer = m_lineUniformBuffer;
    lineBgEntry.size = sizeof(PlexusLineUniforms);

    WGPUBindGroupDescriptor lineBgDesc = {};
    lineBgDesc.layout = m_lineBindGroupLayout;
    lineBgDesc.entryCount = 1;
    lineBgDesc.entries = &lineBgEntry;
    m_lineBindGroup = wgpuDeviceCreateBindGroup(device, &lineBgDesc);

    // Line shader module
    WGPUShaderSourceWGSL lineWgslDesc = {};
    lineWgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    lineWgslDesc.code = toStringView(LINE_SHADER);

    WGPUShaderModuleDescriptor lineShaderDesc = {};
    lineShaderDesc.nextInChain = &lineWgslDesc.chain;
    WGPUShaderModule lineShader = wgpuDeviceCreateShaderModule(device, &lineShaderDesc);

    // Line vertex buffer (quad: 6 vertices for 2 triangles)
    LineVertex lineVerts[6] = {
        {0, 0, 0.0f, -1.0f}, {0, 0, 1.0f, -1.0f}, {0, 0, 1.0f,  1.0f},
        {0, 0, 0.0f, -1.0f}, {0, 0, 1.0f,  1.0f}, {0, 0, 0.0f,  1.0f},
    };

    WGPUBufferDescriptor lineVertDesc = {};
    lineVertDesc.size = sizeof(lineVerts);
    lineVertDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_lineVertexBuffer = wgpuDeviceCreateBuffer(device, &lineVertDesc);
    wgpuQueueWriteBuffer(wgpuDeviceGetQueue(device), m_lineVertexBuffer, 0, lineVerts, sizeof(lineVerts));

    // Line vertex attributes
    WGPUVertexAttribute lineVertAttrs[2] = {};
    lineVertAttrs[0].format = WGPUVertexFormat_Float32x2;
    lineVertAttrs[0].offset = 0;
    lineVertAttrs[0].shaderLocation = 0;
    lineVertAttrs[1].format = WGPUVertexFormat_Float32x2;
    lineVertAttrs[1].offset = 8;
    lineVertAttrs[1].shaderLocation = 1;

    WGPUVertexBufferLayout lineVertLayout = {};
    lineVertLayout.arrayStride = sizeof(LineVertex);
    lineVertLayout.stepMode = WGPUVertexStepMode_Vertex;
    lineVertLayout.attributeCount = 2;
    lineVertLayout.attributes = lineVertAttrs;

    // Line instance attributes (now vec4 + vec4)
    WGPUVertexAttribute lineInstAttrs[2] = {};
    lineInstAttrs[0].format = WGPUVertexFormat_Float32x4;
    lineInstAttrs[0].offset = 0;
    lineInstAttrs[0].shaderLocation = 2;
    lineInstAttrs[1].format = WGPUVertexFormat_Float32x4;
    lineInstAttrs[1].offset = 16;
    lineInstAttrs[1].shaderLocation = 3;

    WGPUVertexBufferLayout lineInstLayout = {};
    lineInstLayout.arrayStride = sizeof(LineInstance);
    lineInstLayout.stepMode = WGPUVertexStepMode_Instance;
    lineInstLayout.attributeCount = 2;
    lineInstLayout.attributes = lineInstAttrs;

    WGPUVertexBufferLayout lineLayouts[2] = {lineVertLayout, lineInstLayout};

    // Line pipeline layout
    WGPUPipelineLayoutDescriptor linePlLayoutDesc = {};
    linePlLayoutDesc.bindGroupLayoutCount = 1;
    linePlLayoutDesc.bindGroupLayouts = &m_lineBindGroupLayout;
    WGPUPipelineLayout linePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &linePlLayoutDesc);

    // Line blend state (additive)
    WGPUBlendState lineBlend = {};
    lineBlend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    lineBlend.color.dstFactor = WGPUBlendFactor_One;
    lineBlend.color.operation = WGPUBlendOperation_Add;
    lineBlend.alpha.srcFactor = WGPUBlendFactor_One;
    lineBlend.alpha.dstFactor = WGPUBlendFactor_One;
    lineBlend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState lineColorTarget = {};
    lineColorTarget.format = EFFECTS_FORMAT;
    lineColorTarget.blend = &lineBlend;
    lineColorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState lineFragState = {};
    lineFragState.module = lineShader;
    lineFragState.entryPoint = toStringView("fs_main");
    lineFragState.targetCount = 1;
    lineFragState.targets = &lineColorTarget;

    WGPURenderPipelineDescriptor linePipeDesc = {};
    linePipeDesc.layout = linePipelineLayout;
    linePipeDesc.vertex.module = lineShader;
    linePipeDesc.vertex.entryPoint = toStringView("vs_main");
    linePipeDesc.vertex.bufferCount = 2;
    linePipeDesc.vertex.buffers = lineLayouts;
    linePipeDesc.fragment = &lineFragState;
    linePipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    linePipeDesc.multisample.count = 1;
    linePipeDesc.multisample.mask = 0xFFFFFFFF;

    m_linePipeline = wgpuDeviceCreateRenderPipeline(device, &linePipeDesc);

    wgpuShaderModuleRelease(lineShader);
    wgpuPipelineLayoutRelease(linePipelineLayout);

    // === NODE PIPELINE ===

    // Node uniform buffer
    WGPUBufferDescriptor nodeUniformDesc = {};
    nodeUniformDesc.size = sizeof(PlexusNodeUniforms);
    nodeUniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_nodeUniformBuffer = wgpuDeviceCreateBuffer(device, &nodeUniformDesc);

    // Node bind group layout
    WGPUBindGroupLayoutEntry nodeLayoutEntry = {};
    nodeLayoutEntry.binding = 0;
    nodeLayoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    nodeLayoutEntry.buffer.type = WGPUBufferBindingType_Uniform;

    WGPUBindGroupLayoutDescriptor nodeLayoutDesc = {};
    nodeLayoutDesc.entryCount = 1;
    nodeLayoutDesc.entries = &nodeLayoutEntry;
    m_nodeBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &nodeLayoutDesc);

    // Node bind group
    WGPUBindGroupEntry nodeBgEntry = {};
    nodeBgEntry.binding = 0;
    nodeBgEntry.buffer = m_nodeUniformBuffer;
    nodeBgEntry.size = sizeof(PlexusNodeUniforms);

    WGPUBindGroupDescriptor nodeBgDesc = {};
    nodeBgDesc.layout = m_nodeBindGroupLayout;
    nodeBgDesc.entryCount = 1;
    nodeBgDesc.entries = &nodeBgEntry;
    m_nodeBindGroup = wgpuDeviceCreateBindGroup(device, &nodeBgDesc);

    // Node shader module
    WGPUShaderSourceWGSL nodeWgslDesc = {};
    nodeWgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    nodeWgslDesc.code = toStringView(NODE_SHADER);

    WGPUShaderModuleDescriptor nodeShaderDesc = {};
    nodeShaderDesc.nextInChain = &nodeWgslDesc.chain;
    WGPUShaderModule nodeShader = wgpuDeviceCreateShaderModule(device, &nodeShaderDesc);

    // Node vertex buffer (circle with 32 segments)
    const int segments = 32;
    std::vector<NodeVertex> nodeVerts;
    nodeVerts.push_back({0.0f, 0.0f});  // Center
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159f;
        nodeVerts.push_back({std::cos(angle), std::sin(angle)});
    }

    WGPUBufferDescriptor nodeVertDesc = {};
    nodeVertDesc.size = nodeVerts.size() * sizeof(NodeVertex);
    nodeVertDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_nodeVertexBuffer = wgpuDeviceCreateBuffer(device, &nodeVertDesc);
    wgpuQueueWriteBuffer(wgpuDeviceGetQueue(device), m_nodeVertexBuffer, 0,
                         nodeVerts.data(), nodeVerts.size() * sizeof(NodeVertex));

    // Node index buffer (triangle fan)
    std::vector<uint16_t> nodeIndices;
    for (int i = 0; i < segments; i++) {
        nodeIndices.push_back(0);
        nodeIndices.push_back(i + 1);
        nodeIndices.push_back(i + 2);
    }
    m_nodeIndexCount = static_cast<uint32_t>(nodeIndices.size());

    WGPUBufferDescriptor nodeIdxDesc = {};
    nodeIdxDesc.size = nodeIndices.size() * sizeof(uint16_t);
    nodeIdxDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    m_nodeIndexBuffer = wgpuDeviceCreateBuffer(device, &nodeIdxDesc);
    wgpuQueueWriteBuffer(wgpuDeviceGetQueue(device), m_nodeIndexBuffer, 0,
                         nodeIndices.data(), nodeIndices.size() * sizeof(uint16_t));

    // Node vertex attributes
    WGPUVertexAttribute nodeVertAttr = {};
    nodeVertAttr.format = WGPUVertexFormat_Float32x2;
    nodeVertAttr.offset = 0;
    nodeVertAttr.shaderLocation = 0;

    WGPUVertexBufferLayout nodeVertLayout = {};
    nodeVertLayout.arrayStride = sizeof(NodeVertex);
    nodeVertLayout.stepMode = WGPUVertexStepMode_Vertex;
    nodeVertLayout.attributeCount = 1;
    nodeVertLayout.attributes = &nodeVertAttr;

    // Node instance attributes (vec4 + vec4)
    WGPUVertexAttribute nodeInstAttrs[2] = {};
    nodeInstAttrs[0].format = WGPUVertexFormat_Float32x4;
    nodeInstAttrs[0].offset = 0;
    nodeInstAttrs[0].shaderLocation = 1;
    nodeInstAttrs[1].format = WGPUVertexFormat_Float32x4;
    nodeInstAttrs[1].offset = 16;
    nodeInstAttrs[1].shaderLocation = 2;

    WGPUVertexBufferLayout nodeInstLayout = {};
    nodeInstLayout.arrayStride = sizeof(NodeInstance);
    nodeInstLayout.stepMode = WGPUVertexStepMode_Instance;
    nodeInstLayout.attributeCount = 2;
    nodeInstLayout.attributes = nodeInstAttrs;

    WGPUVertexBufferLayout nodeLayouts[2] = {nodeVertLayout, nodeInstLayout};

    // Node pipeline layout
    WGPUPipelineLayoutDescriptor nodePlLayoutDesc = {};
    nodePlLayoutDesc.bindGroupLayoutCount = 1;
    nodePlLayoutDesc.bindGroupLayouts = &m_nodeBindGroupLayout;
    WGPUPipelineLayout nodePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &nodePlLayoutDesc);

    // Node blend state (additive)
    WGPUBlendState nodeBlend = {};
    nodeBlend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    nodeBlend.color.dstFactor = WGPUBlendFactor_One;
    nodeBlend.color.operation = WGPUBlendOperation_Add;
    nodeBlend.alpha.srcFactor = WGPUBlendFactor_One;
    nodeBlend.alpha.dstFactor = WGPUBlendFactor_One;
    nodeBlend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState nodeColorTarget = {};
    nodeColorTarget.format = EFFECTS_FORMAT;
    nodeColorTarget.blend = &nodeBlend;
    nodeColorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState nodeFragState = {};
    nodeFragState.module = nodeShader;
    nodeFragState.entryPoint = toStringView("fs_main");
    nodeFragState.targetCount = 1;
    nodeFragState.targets = &nodeColorTarget;

    WGPURenderPipelineDescriptor nodePipeDesc = {};
    nodePipeDesc.layout = nodePipelineLayout;
    nodePipeDesc.vertex.module = nodeShader;
    nodePipeDesc.vertex.entryPoint = toStringView("vs_main");
    nodePipeDesc.vertex.bufferCount = 2;
    nodePipeDesc.vertex.buffers = nodeLayouts;
    nodePipeDesc.fragment = &nodeFragState;
    nodePipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    nodePipeDesc.multisample.count = 1;
    nodePipeDesc.multisample.mask = 0xFFFFFFFF;

    m_nodePipeline = wgpuDeviceCreateRenderPipeline(device, &nodePipeDesc);

    wgpuShaderModuleRelease(nodeShader);
    wgpuPipelineLayoutRelease(nodePipelineLayout);
}

void Plexus::renderLines(Context& ctx) {
    if (m_lines.empty()) return;

    WGPUDevice device = ctx.device();
    WGPUQueue queue = ctx.queue();

    // Ensure instance buffer capacity
    size_t neededSize = m_lines.size() * sizeof(LineInstance);
    if (neededSize > m_lineInstanceCapacity) {
        if (m_lineInstanceBuffer) {
            wgpuBufferRelease(m_lineInstanceBuffer);
        }
        m_lineInstanceCapacity = neededSize * 2;
        WGPUBufferDescriptor desc = {};
        desc.size = m_lineInstanceCapacity;
        desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        m_lineInstanceBuffer = wgpuDeviceCreateBuffer(device, &desc);
    }

    // Upload instance data
    wgpuQueueWriteBuffer(queue, m_lineInstanceBuffer, 0, m_lines.data(), neededSize);

    // Build view-projection matrix
    float aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);

    // Upload uniforms
    PlexusLineUniforms uniforms;
    buildViewProjection(uniforms.viewProj, m_cameraDistance, m_cameraAngle, aspectRatio, m_3dEnabled);
    uniforms.resolution[0] = static_cast<float>(m_width);
    uniforms.resolution[1] = static_cast<float>(m_height);
    uniforms.lineWidth = m_lineWidth;
    uniforms._pad1 = 0;
    uniforms.color[0] = m_lineColor.r;
    uniforms.color[1] = m_lineColor.g;
    uniforms.color[2] = m_lineColor.b;
    uniforms.color[3] = m_lineColor.a;
    wgpuQueueWriteBuffer(queue, m_lineUniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create render pass (load existing content)
    WGPURenderPassColorAttachment colorAtt = {};
    colorAtt.view = m_outputView;
    colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAtt.loadOp = WGPULoadOp_Load;
    colorAtt.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAtt;

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderSetPipeline(pass, m_linePipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_lineBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_lineVertexBuffer, 0, 6 * sizeof(LineVertex));
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m_lineInstanceBuffer, 0, neededSize);
    wgpuRenderPassEncoderDraw(pass, 6, static_cast<uint32_t>(m_lines.size()), 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(queue, 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);
    wgpuCommandEncoderRelease(encoder);
}

void Plexus::renderNodes(Context& ctx) {
    if (m_nodes.empty()) return;

    WGPUDevice device = ctx.device();
    WGPUQueue queue = ctx.queue();

    // Build node instances
    m_nodeInstances.resize(m_nodes.size());
    for (size_t i = 0; i < m_nodes.size(); i++) {
        m_nodeInstances[i].position = glm::vec4(m_nodes[i].position, m_nodeSize);
        m_nodeInstances[i].color = m_nodeColor;
    }

    // Ensure instance buffer capacity
    size_t neededSize = m_nodeInstances.size() * sizeof(NodeInstance);
    if (neededSize > m_nodeInstanceCapacity) {
        if (m_nodeInstanceBuffer) {
            wgpuBufferRelease(m_nodeInstanceBuffer);
        }
        m_nodeInstanceCapacity = neededSize * 2;
        WGPUBufferDescriptor desc = {};
        desc.size = m_nodeInstanceCapacity;
        desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        m_nodeInstanceBuffer = wgpuDeviceCreateBuffer(device, &desc);
    }

    // Upload instance data
    wgpuQueueWriteBuffer(queue, m_nodeInstanceBuffer, 0, m_nodeInstances.data(), neededSize);

    // Build view-projection matrix
    float aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);

    // Upload uniforms
    PlexusNodeUniforms uniforms;
    buildViewProjection(uniforms.viewProj, m_cameraDistance, m_cameraAngle, aspectRatio, m_3dEnabled);
    uniforms.resolution[0] = static_cast<float>(m_width);
    uniforms.resolution[1] = static_cast<float>(m_height);
    uniforms.aspectRatio = aspectRatio;
    uniforms._pad = 0;
    wgpuQueueWriteBuffer(queue, m_nodeUniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create render pass (load existing content)
    WGPURenderPassColorAttachment colorAtt = {};
    colorAtt.view = m_outputView;
    colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAtt.loadOp = WGPULoadOp_Load;
    colorAtt.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAtt;

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderSetPipeline(pass, m_nodePipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_nodeBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_nodeVertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m_nodeInstanceBuffer, 0, neededSize);
    wgpuRenderPassEncoderSetIndexBuffer(pass, m_nodeIndexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, m_nodeIndexCount, static_cast<uint32_t>(m_nodes.size()), 0, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(queue, 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);
    wgpuCommandEncoderRelease(encoder);
}

void Plexus::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }
    // Generators use their declared resolution (default 1280x720)

    // Plexus is a simulation - always cooks

    if (!m_nodesInitialized) {
        initNodes();
    }

    float dt = ctx.dt();
    float time = static_cast<float>(ctx.time());

    // Update physics
    updateNodes(dt, time);

    // Find connections
    findConnections();

    // Clear output texture
    WGPURenderPassColorAttachment colorAtt = {};
    colorAtt.view = m_outputView;
    colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAtt.loadOp = WGPULoadOp_Clear;
    colorAtt.storeOp = WGPUStoreOp_Store;
    colorAtt.clearValue = {m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAtt;

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), nullptr);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);
    wgpuCommandEncoderRelease(encoder);

    // Render lines first (behind nodes)
    renderLines(ctx);

    // Render nodes on top
    renderNodes(ctx);

    didCook();
}

void Plexus::cleanup() {
    if (m_linePipeline) { wgpuRenderPipelineRelease(m_linePipeline); m_linePipeline = nullptr; }
    if (m_lineVertexBuffer) { wgpuBufferRelease(m_lineVertexBuffer); m_lineVertexBuffer = nullptr; }
    if (m_lineInstanceBuffer) { wgpuBufferRelease(m_lineInstanceBuffer); m_lineInstanceBuffer = nullptr; }
    if (m_lineUniformBuffer) { wgpuBufferRelease(m_lineUniformBuffer); m_lineUniformBuffer = nullptr; }
    if (m_lineBindGroupLayout) { wgpuBindGroupLayoutRelease(m_lineBindGroupLayout); m_lineBindGroupLayout = nullptr; }
    if (m_lineBindGroup) { wgpuBindGroupRelease(m_lineBindGroup); m_lineBindGroup = nullptr; }

    if (m_nodePipeline) { wgpuRenderPipelineRelease(m_nodePipeline); m_nodePipeline = nullptr; }
    if (m_nodeVertexBuffer) { wgpuBufferRelease(m_nodeVertexBuffer); m_nodeVertexBuffer = nullptr; }
    if (m_nodeIndexBuffer) { wgpuBufferRelease(m_nodeIndexBuffer); m_nodeIndexBuffer = nullptr; }
    if (m_nodeInstanceBuffer) { wgpuBufferRelease(m_nodeInstanceBuffer); m_nodeInstanceBuffer = nullptr; }
    if (m_nodeUniformBuffer) { wgpuBufferRelease(m_nodeUniformBuffer); m_nodeUniformBuffer = nullptr; }
    if (m_nodeBindGroupLayout) { wgpuBindGroupLayoutRelease(m_nodeBindGroupLayout); m_nodeBindGroupLayout = nullptr; }
    if (m_nodeBindGroup) { wgpuBindGroupRelease(m_nodeBindGroup); m_nodeBindGroup = nullptr; }

    releaseOutput();
    m_initialized = false;
    m_nodesInitialized = false;
}

} // namespace vivid::effects
