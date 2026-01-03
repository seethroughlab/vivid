// Vivid Effects 2D - Image Operator Implementation

#include <vivid/effects/image.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <string>
#include <vivid/io/image_loader.h>
#include <vivid/context.h>
#include <iostream>
#include <cstring>

namespace vivid::effects {

struct ImageUniforms {
    float _unused;
    float _pad1;
    float _pad2;
    float _pad3;
};

Image::~Image() {
    cleanup();
}

void Image::init(Context& ctx) {
    // Skip if already initialized with same file
    if (isInitialized() && file.get() == m_loadedPath) return;

    // Allow re-initialization when file changes
    if (isInitialized() && file.get() != m_loadedPath) {
        resetInit();
    }

    if (!beginInit()) return;

    if (!file.empty()) {
        loadImage(ctx);
    }

    if (!m_pipeline) {
        createPipeline(ctx);
    }
}

void Image::createPipeline(Context& ctx) {
    // Simple pass-through fragment shader
    const char* fragmentShader = R"(
struct Uniforms {
    _unused: f32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return textureSample(inputTex, texSampler, input.uv);
}
)";

    // Combine shared vertex shader with fragment shader
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(ImageUniforms))
           .texture(1)
           .sampler(2);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create uniform buffer (even though it's unused)
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(ImageUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());
}

void Image::loadImage(Context& ctx) {
    // Load image via vivid-io
    const std::string& filePath = file.get();
    auto imageData = vivid::io::loadImage(filePath);

    if (!imageData.valid()) {
        std::cerr << "Image: Failed to load: " << filePath << std::endl;
        return;
    }

    int width = imageData.width;
    int height = imageData.height;

    // Release old loaded texture if exists
    if (m_loadedTexture) {
        wgpuTextureRelease(m_loadedTexture);
        m_loadedTexture = nullptr;
    }
    if (m_loadedTextureView) {
        wgpuTextureViewRelease(m_loadedTextureView);
        m_loadedTextureView = nullptr;
    }

    // Store dimensions
    m_width = width;
    m_height = height;

    // Store CPU pixel data if requested (for pixel sampling)
    if (keepCpuData.get()) {
        m_cpuPixels = imageData.pixels;  // Copy RGBA data
        m_cpuWidth = width;
        m_cpuHeight = height;
    } else {
        m_cpuPixels.clear();
        m_cpuWidth = 0;
        m_cpuHeight = 0;
    }

    // Create output texture for rendering
    createOutput(ctx);

    // Create GPU texture for loaded image using EFFECTS_FORMAT for compatibility
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("Image Loaded Texture");
    texDesc.size.width = width;
    texDesc.size.height = height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = EFFECTS_FORMAT;  // RGBA16Float for compatibility
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    m_loadedTexture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = EFFECTS_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    m_loadedTextureView = wgpuTextureCreateView(m_loadedTexture, &viewDesc);

    // Convert 8-bit RGBA to 16-bit float RGBA
    std::vector<uint16_t> floatPixels(width * height * 4);
    for (size_t i = 0; i < imageData.pixels.size(); ++i) {
        // Convert uint8 [0-255] to float16 [0.0-1.0]
        float normalized = imageData.pixels[i] / 255.0f;
        // Convert float32 to float16 using simple bit manipulation
        // This is a basic conversion - for values in [0,1] range it works well
        uint32_t f32;
        std::memcpy(&f32, &normalized, sizeof(float));
        uint32_t sign = (f32 >> 16) & 0x8000;
        uint32_t exp = ((f32 >> 23) & 0xFF) - 127 + 15;
        uint32_t mant = (f32 >> 13) & 0x3FF;
        if (exp <= 0) {
            floatPixels[i] = sign;  // Denormalized or zero
        } else if (exp >= 31) {
            floatPixels[i] = sign | 0x7C00;  // Infinity
        } else {
            floatPixels[i] = sign | (exp << 10) | mant;
        }
    }

    // Upload pixel data
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = m_loadedTexture;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = width * 4 * sizeof(uint16_t);  // 8 bytes per pixel (RGBA16Float)
    dataLayout.rowsPerImage = height;

    WGPUExtent3D writeSize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    wgpuQueueWriteTexture(ctx.queue(), &destination, floatPixels.data(),
                          floatPixels.size() * sizeof(uint16_t), &dataLayout, &writeSize);

    m_loadedPath = filePath;
    std::cout << "Image: Loaded " << filePath << " (" << width << "x" << height << ")" << std::endl;
}

void Image::process(Context& ctx) {
    // Check if file changed or not initialized
    if (!isInitialized() || file.get() != m_loadedPath) {
        init(ctx);
    }
    // Image uses loaded file dimensions - no auto-resize

    if (!m_loadedTextureView) return;  // No image loaded yet

    if (!needsCook()) return;

    // Update uniforms (unused, but required by pipeline)
    ImageUniforms uniforms = {0.0f, 0.0f, 0.0f, 0.0f};
    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group for this frame
    WGPUBindGroupEntry bindEntries[3] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(ImageUniforms);
    bindEntries[1].binding = 1;
    bindEntries[1].textureView = m_loadedTextureView;
    bindEntries[2].binding = 2;
    bindEntries[2].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 3;
    bindDesc.entries = bindEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    // Use shared command encoder for batched submission
    WGPUCommandEncoder encoder = ctx.gpuEncoder();

    // Begin render pass
    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);

    // Draw
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    // End render pass
    endRenderPass(pass, encoder, ctx);

    wgpuBindGroupRelease(bindGroup);

    didCook();
}

void Image::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    // Note: m_sampler is managed by gpu_common cache, do not release
    m_sampler = nullptr;

    if (m_loadedTexture) {
        wgpuTextureRelease(m_loadedTexture);
        m_loadedTexture = nullptr;
    }
    if (m_loadedTextureView) {
        wgpuTextureViewRelease(m_loadedTextureView);
        m_loadedTextureView = nullptr;
    }

    // Clear CPU pixel data
    m_cpuPixels.clear();
    m_cpuWidth = 0;
    m_cpuHeight = 0;

    releaseOutput();
    resetInit();
}

glm::vec4 Image::getPixel(int x, int y) const {
    if (m_cpuPixels.empty() || x < 0 || y < 0 || x >= m_cpuWidth || y >= m_cpuHeight) {
        return glm::vec4(0.0f);  // Return black for invalid coordinates
    }

    size_t idx = (y * m_cpuWidth + x) * 4;
    return glm::vec4(
        m_cpuPixels[idx] / 255.0f,
        m_cpuPixels[idx + 1] / 255.0f,
        m_cpuPixels[idx + 2] / 255.0f,
        m_cpuPixels[idx + 3] / 255.0f
    );
}

glm::vec4 Image::getAverageColor(int x, int y, int w, int h) const {
    if (m_cpuPixels.empty() || w <= 0 || h <= 0) {
        return glm::vec4(0.0f);
    }

    // Clamp region to image bounds
    int x1 = std::max(0, x);
    int y1 = std::max(0, y);
    int x2 = std::min(m_cpuWidth, x + w);
    int y2 = std::min(m_cpuHeight, y + h);

    if (x1 >= x2 || y1 >= y2) {
        return glm::vec4(0.0f);
    }

    double sumR = 0, sumG = 0, sumB = 0, sumA = 0;
    int count = 0;

    for (int py = y1; py < y2; ++py) {
        for (int px = x1; px < x2; ++px) {
            size_t idx = (py * m_cpuWidth + px) * 4;
            sumR += m_cpuPixels[idx];
            sumG += m_cpuPixels[idx + 1];
            sumB += m_cpuPixels[idx + 2];
            sumA += m_cpuPixels[idx + 3];
            ++count;
        }
    }

    if (count == 0) {
        return glm::vec4(0.0f);
    }

    return glm::vec4(
        static_cast<float>(sumR / count / 255.0),
        static_cast<float>(sumG / count / 255.0),
        static_cast<float>(sumB / count / 255.0),
        static_cast<float>(sumA / count / 255.0)
    );
}

} // namespace vivid::effects
