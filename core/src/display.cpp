// Vivid - Display Implementation

#include <vivid/display.h>
#include <vivid/asset_loader.h>
#include <iostream>
#include <vector>
#include <cstring>

namespace vivid {

// Helper to create WGPUStringView from C string
static inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

// Embedded 8x8 bitmap font (ASCII 32-127, 96 characters)
// Each character is 8 bytes (8 rows of 8 bits)
static const uint8_t FONT_DATA[] = {
    // Space (32)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ! (33)
    0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00,
    // " (34)
    0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00,
    // # (35)
    0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00,
    // $ (36)
    0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00,
    // % (37)
    0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00,
    // & (38)
    0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00,
    // ' (39)
    0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ( (40)
    0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00,
    // ) (41)
    0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00,
    // * (42)
    0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00,
    // + (43)
    0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00,
    // , (44)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30,
    // - (45)
    0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00,
    // . (46)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00,
    // / (47)
    0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00,
    // 0 (48)
    0x7C, 0xC6, 0xCE, 0xD6, 0xE6, 0xC6, 0x7C, 0x00,
    // 1 (49)
    0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00,
    // 2 (50)
    0x7C, 0xC6, 0x06, 0x1C, 0x30, 0x66, 0xFE, 0x00,
    // 3 (51)
    0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0x00,
    // 4 (52)
    0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x1E, 0x00,
    // 5 (53)
    0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00,
    // 6 (54)
    0x38, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00,
    // 7 (55)
    0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00,
    // 8 (56)
    0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00,
    // 9 (57)
    0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00,
    // : (58)
    0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00,
    // ; (59)
    0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30,
    // < (60)
    0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00,
    // = (61)
    0x00, 0x00, 0x7E, 0x00, 0x00, 0x7E, 0x00, 0x00,
    // > (62)
    0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00,
    // ? (63)
    0x7C, 0xC6, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00,
    // @ (64)
    0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x78, 0x00,
    // A (65)
    0x38, 0x6C, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00,
    // B (66)
    0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0x00,
    // C (67)
    0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0x00,
    // D (68)
    0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00,
    // E (69)
    0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0x00,
    // F (70)
    0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0x00,
    // G (71)
    0x3C, 0x66, 0xC0, 0xC0, 0xCE, 0x66, 0x3A, 0x00,
    // H (72)
    0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00,
    // I (73)
    0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00,
    // J (74)
    0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00,
    // K (75)
    0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0x00,
    // L (76)
    0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00,
    // M (77)
    0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0x00,
    // N (78)
    0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00,
    // O (79)
    0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00,
    // P (80)
    0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00,
    // Q (81)
    0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0x7C, 0x0E, 0x00,
    // R (82)
    0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0x00,
    // S (83)
    0x7C, 0xC6, 0x60, 0x38, 0x0C, 0xC6, 0x7C, 0x00,
    // T (84)
    0x7E, 0x7E, 0x5A, 0x18, 0x18, 0x18, 0x3C, 0x00,
    // U (85)
    0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00,
    // V (86)
    0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00,
    // W (87)
    0xC6, 0xC6, 0xC6, 0xD6, 0xD6, 0xFE, 0x6C, 0x00,
    // X (88)
    0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0x00,
    // Y (89)
    0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x00,
    // Z (90)
    0xFE, 0xC6, 0x8C, 0x18, 0x32, 0x66, 0xFE, 0x00,
    // [ (91)
    0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00,
    // \ (92)
    0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00,
    // ] (93)
    0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00,
    // ^ (94)
    0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00,
    // _ (95)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    // ` (96)
    0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00,
    // a (97)
    0x00, 0x00, 0x78, 0x0C, 0x7C, 0xCC, 0x76, 0x00,
    // b (98)
    0xE0, 0x60, 0x7C, 0x66, 0x66, 0x66, 0xDC, 0x00,
    // c (99)
    0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0x00,
    // d (100)
    0x1C, 0x0C, 0x7C, 0xCC, 0xCC, 0xCC, 0x76, 0x00,
    // e (101)
    0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0x00,
    // f (102)
    0x3C, 0x66, 0x60, 0xF8, 0x60, 0x60, 0xF0, 0x00,
    // g (103)
    0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x78,
    // h (104)
    0xE0, 0x60, 0x6C, 0x76, 0x66, 0x66, 0xE6, 0x00,
    // i (105)
    0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00,
    // j (106)
    0x06, 0x00, 0x06, 0x06, 0x06, 0x66, 0x66, 0x3C,
    // k (107)
    0xE0, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0xE6, 0x00,
    // l (108)
    0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00,
    // m (109)
    0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xD6, 0xD6, 0x00,
    // n (110)
    0x00, 0x00, 0xDC, 0x66, 0x66, 0x66, 0x66, 0x00,
    // o (111)
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00,
    // p (112)
    0x00, 0x00, 0xDC, 0x66, 0x66, 0x7C, 0x60, 0xF0,
    // q (113)
    0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x1E,
    // r (114)
    0x00, 0x00, 0xDC, 0x76, 0x60, 0x60, 0xF0, 0x00,
    // s (115)
    0x00, 0x00, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x00,
    // t (116)
    0x30, 0x30, 0xFC, 0x30, 0x30, 0x36, 0x1C, 0x00,
    // u (117)
    0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0x00,
    // v (118)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00,
    // w (119)
    0x00, 0x00, 0xC6, 0xD6, 0xD6, 0xFE, 0x6C, 0x00,
    // x (120)
    0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00,
    // y (121)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x7C,
    // z (122)
    0x00, 0x00, 0x7E, 0x4C, 0x18, 0x32, 0x7E, 0x00,
    // { (123)
    0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00,
    // | (124)
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00,
    // } (125)
    0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00,
    // ~ (126)
    0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // DEL (127) - block
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

Display::Display(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surfaceFormat)
    : m_device(device)
    , m_queue(queue)
    , m_surfaceFormat(surfaceFormat)
{
    m_valid = createBlitPipeline();
    if (m_valid) {
        m_valid = createTextPipeline();
    }
}

Display::~Display() {
    shutdown();
}

void Display::shutdown() {
    if (m_blitBindGroup) { wgpuBindGroupRelease(m_blitBindGroup); m_blitBindGroup = nullptr; }
    m_lastBlitTexture = nullptr;
    if (m_blitPipeline) { wgpuRenderPipelineRelease(m_blitPipeline); m_blitPipeline = nullptr; }
    if (m_sampler) { wgpuSamplerRelease(m_sampler); m_sampler = nullptr; }
    if (m_blitBindGroupLayout) { wgpuBindGroupLayoutRelease(m_blitBindGroupLayout); m_blitBindGroupLayout = nullptr; }
    if (m_textPipeline) { wgpuRenderPipelineRelease(m_textPipeline); m_textPipeline = nullptr; }
    if (m_fontTexture) { wgpuTextureRelease(m_fontTexture); m_fontTexture = nullptr; }
    if (m_fontTextureView) { wgpuTextureViewRelease(m_fontTextureView); m_fontTextureView = nullptr; }
    if (m_textBindGroupLayout) { wgpuBindGroupLayoutRelease(m_textBindGroupLayout); m_textBindGroupLayout = nullptr; }
    if (m_textBindGroup) { wgpuBindGroupRelease(m_textBindGroup); m_textBindGroup = nullptr; }
    if (m_textUniformBuffer) { wgpuBufferRelease(m_textUniformBuffer); m_textUniformBuffer = nullptr; }
    if (m_textVertexBuffer) { wgpuBufferRelease(m_textVertexBuffer); m_textVertexBuffer = nullptr; }
    if (m_fontSampler) { wgpuSamplerRelease(m_fontSampler); m_fontSampler = nullptr; }
    m_valid = false;
}

bool Display::createBlitPipeline() {
    auto& assets = AssetLoader::instance();
    auto exeDir = assets.executableDir();
    std::cout << "Looking for shader: blit.wgsl from exeDir: " << exeDir << std::endl;

    std::string shaderCode = assets.loadShader("blit.wgsl");
    if (!shaderCode.empty()) {
        std::cout << "Found shader at: " << assets.resolve("shaders/blit.wgsl") << std::endl;
        std::cout << "Shader preview: " << shaderCode.substr(0, 100) << "..." << std::endl;
    }
    if (shaderCode.empty()) {
        std::cerr << "Failed to load blit shader" << std::endl;
        return false;
    }

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderCode.c_str());

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("Blit Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(m_device, &shaderDesc);
    if (!shaderModule) {
        std::cerr << "Failed to create blit shader module" << std::endl;
        return false;
    }

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.lodMinClamp = 0.0f;
    samplerDesc.lodMaxClamp = 1.0f;
    samplerDesc.maxAnisotropy = 1;

    m_sampler = wgpuDeviceCreateSampler(m_device, &samplerDesc);
    if (!m_sampler) {
        wgpuShaderModuleRelease(shaderModule);
        return false;
    }

    // Create bind group layout
    WGPUBindGroupLayoutEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].texture.multisampled = false;

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
    bindGroupLayoutDesc.label = toStringView("Blit Bind Group Layout");
    bindGroupLayoutDesc.entryCount = 2;
    bindGroupLayoutDesc.entries = entries;

    m_blitBindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_device, &bindGroupLayoutDesc);
    if (!m_blitBindGroupLayout) {
        wgpuShaderModuleRelease(shaderModule);
        return false;
    }

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.label = toStringView("Blit Pipeline Layout");
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_blitBindGroupLayout;

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);
    if (!pipelineLayout) {
        wgpuShaderModuleRelease(shaderModule);
        return false;
    }

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = toStringView("Blit Pipeline");
    pipelineDesc.layout = pipelineLayout;

    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 0;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = m_surfaceFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    m_blitPipeline = wgpuDeviceCreateRenderPipeline(m_device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);

    if (!m_blitPipeline) {
        std::cerr << "Failed to create blit pipeline" << std::endl;
        return false;
    }

    std::cout << "Blit pipeline created successfully" << std::endl;
    return true;
}

bool Display::createTextPipeline() {
    // Load text shader
    auto& assets = AssetLoader::instance();
    auto exeDir = assets.executableDir();
    std::cout << "Looking for shader: text.wgsl from exeDir: " << exeDir << std::endl;

    std::string shaderCode = assets.loadShader("text.wgsl");
    if (!shaderCode.empty()) {
        std::cout << "Found shader at: " << assets.resolve("shaders/text.wgsl") << std::endl;
        std::cout << "Shader preview: " << shaderCode.substr(0, 100) << "..." << std::endl;
    }
    if (shaderCode.empty()) {
        std::cerr << "Failed to load text shader" << std::endl;
        return false;
    }

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderCode.c_str());

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("Text Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(m_device, &shaderDesc);
    if (!shaderModule) {
        std::cerr << "Failed to create text shader module" << std::endl;
        return false;
    }

    // Create font texture (128x64: 16 chars x 8 rows, each char 8x8)
    const int texWidth = 128;
    const int texHeight = 64;
    std::vector<uint8_t> textureData(texWidth * texHeight, 0);

    // Fill texture with font data
    for (int charIdx = 0; charIdx < 96; charIdx++) {
        int charX = (charIdx % 16) * 8;
        int charY = (charIdx / 16) * 8;

        for (int row = 0; row < 8; row++) {
            uint8_t rowBits = FONT_DATA[charIdx * 8 + row];
            for (int col = 0; col < 8; col++) {
                if (rowBits & (0x80 >> col)) {
                    int px = charX + col;
                    int py = charY + row;
                    textureData[py * texWidth + px] = 255;
                }
            }
        }
    }

    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("Font Texture");
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {(uint32_t)texWidth, (uint32_t)texHeight, 1};
    texDesc.format = WGPUTextureFormat_R8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    m_fontTexture = wgpuDeviceCreateTexture(m_device, &texDesc);
    if (!m_fontTexture) {
        wgpuShaderModuleRelease(shaderModule);
        return false;
    }

    // Upload texture data
    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = texWidth;
    dataLayout.rowsPerImage = texHeight;

    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = m_fontTexture;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUExtent3D writeSize = {(uint32_t)texWidth, (uint32_t)texHeight, 1};
    wgpuQueueWriteTexture(m_queue, &destination, textureData.data(), textureData.size(), &dataLayout, &writeSize);

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_R8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    m_fontTextureView = wgpuTextureCreateView(m_fontTexture, &viewDesc);

    // Create font sampler (nearest neighbor for crisp text)
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Nearest;
    samplerDesc.minFilter = WGPUFilterMode_Nearest;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.lodMinClamp = 0.0f;
    samplerDesc.lodMaxClamp = 1.0f;
    samplerDesc.maxAnisotropy = 1;
    m_fontSampler = wgpuDeviceCreateSampler(m_device, &samplerDesc);

    // Create uniform buffer
    WGPUBufferDescriptor uniformBufferDesc = {};
    uniformBufferDesc.label = toStringView("Text Uniform Buffer");
    uniformBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBufferDesc.size = 16;  // vec2f screenSize + padding
    uniformBufferDesc.mappedAtCreation = false;
    m_textUniformBuffer = wgpuDeviceCreateBuffer(m_device, &uniformBufferDesc);

    // Create vertex buffer (6 vertices per char, 8 floats per vertex: pos(2) + uv(2) + color(4))
    WGPUBufferDescriptor vertexBufferDesc = {};
    vertexBufferDesc.label = toStringView("Text Vertex Buffer");
    vertexBufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    vertexBufferDesc.size = MAX_TEXT_CHARS * 6 * 8 * sizeof(float);
    vertexBufferDesc.mappedAtCreation = false;
    m_textVertexBuffer = wgpuDeviceCreateBuffer(m_device, &vertexBufferDesc);

    // Create bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[3] = {};

    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Vertex;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].buffer.minBindingSize = 16;

    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
    bindGroupLayoutDesc.label = toStringView("Text Bind Group Layout");
    bindGroupLayoutDesc.entryCount = 3;
    bindGroupLayoutDesc.entries = layoutEntries;
    m_textBindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_device, &bindGroupLayoutDesc);

    // Create bind group
    WGPUBindGroupEntry bindGroupEntries[3] = {};
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = m_textUniformBuffer;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = 16;

    bindGroupEntries[1].binding = 1;
    bindGroupEntries[1].sampler = m_fontSampler;

    bindGroupEntries[2].binding = 2;
    bindGroupEntries[2].textureView = m_fontTextureView;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.label = toStringView("Text Bind Group");
    bindGroupDesc.layout = m_textBindGroupLayout;
    bindGroupDesc.entryCount = 3;
    bindGroupDesc.entries = bindGroupEntries;
    m_textBindGroup = wgpuDeviceCreateBindGroup(m_device, &bindGroupDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.label = toStringView("Text Pipeline Layout");
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_textBindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);

    // Vertex attributes
    WGPUVertexAttribute attributes[3] = {};
    attributes[0].format = WGPUVertexFormat_Float32x2;  // position
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;

    attributes[1].format = WGPUVertexFormat_Float32x2;  // uv
    attributes[1].offset = 2 * sizeof(float);
    attributes[1].shaderLocation = 1;

    attributes[2].format = WGPUVertexFormat_Float32x4;  // color
    attributes[2].offset = 4 * sizeof(float);
    attributes[2].shaderLocation = 2;

    WGPUVertexBufferLayout vertexBufferLayout = {};
    vertexBufferLayout.arrayStride = 8 * sizeof(float);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = 3;
    vertexBufferLayout.attributes = attributes;

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = toStringView("Text Pipeline");
    pipelineDesc.layout = pipelineLayout;

    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;

    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = m_surfaceFormat;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    m_textPipeline = wgpuDeviceCreateRenderPipeline(m_device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);

    if (!m_textPipeline) {
        std::cerr << "Failed to create text pipeline" << std::endl;
        return false;
    }

    std::cout << "Text pipeline created successfully" << std::endl;
    return true;
}

void Display::setScreenSize(int width, int height) {
    m_screenWidth = width;
    m_screenHeight = height;

    // Update uniform buffer
    float uniforms[4] = {(float)width, (float)height, 0.0f, 0.0f};
    wgpuQueueWriteBuffer(m_queue, m_textUniformBuffer, 0, uniforms, sizeof(uniforms));
}

void Display::blit(WGPURenderPassEncoder pass, WGPUTextureView texture) {
    if (!m_blitPipeline || !texture) {
        std::cerr << "Blit early return: pipeline=" << m_blitPipeline << ", texture=" << texture << std::endl;
        return;
    }

    // Update cached bind group if texture changed
    if (texture != m_lastBlitTexture || !m_blitBindGroup) {
        if (m_blitBindGroup) {
            wgpuBindGroupRelease(m_blitBindGroup);
        }

        WGPUBindGroupEntry entries[2] = {};
        entries[0].binding = 0;
        entries[0].sampler = m_sampler;
        entries[1].binding = 1;
        entries[1].textureView = texture;

        WGPUBindGroupDescriptor bindGroupDesc = {};
        bindGroupDesc.label = toStringView("Blit Bind Group");
        bindGroupDesc.layout = m_blitBindGroupLayout;
        bindGroupDesc.entryCount = 2;
        bindGroupDesc.entries = entries;

        m_blitBindGroup = wgpuDeviceCreateBindGroup(m_device, &bindGroupDesc);
        m_lastBlitTexture = texture;

        if (!m_blitBindGroup) {
            std::cerr << "Blit: failed to create bind group" << std::endl;
            return;
        }
    }

    // Set viewport to cover the entire screen
    wgpuRenderPassEncoderSetViewport(pass, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0, 1);
    wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, m_screenWidth, m_screenHeight);

    wgpuRenderPassEncoderSetPipeline(pass, m_blitPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_blitBindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
}

void Display::renderText(WGPURenderPassEncoder pass, const std::string& text,
                         float x, float y, float scale) {
    if (!m_textPipeline || text.empty()) return;

    // Truncate text to avoid buffer overflow (MAX_TEXT_CHARS defined in header)
    std::string displayText = text;
    if (displayText.length() > MAX_TEXT_CHARS - 3) {
        displayText = displayText.substr(0, MAX_TEXT_CHARS - 3) + "...";
    }

    // Build vertex data
    std::vector<float> vertices;
    vertices.reserve(displayText.length() * 6 * 8);

    const float charWidth = FONT_CHAR_WIDTH * scale;
    const float charHeight = FONT_CHAR_HEIGHT * scale;
    const float texCharWidth = 8.0f / 128.0f;   // 8 pixels / 128 texture width
    const float texCharHeight = 8.0f / 64.0f;   // 8 pixels / 64 texture height

    float cursorX = x;
    float cursorY = y;

    // Color: orange/yellow for visibility
    const float r = 1.0f, g = 0.8f, b = 0.2f, a = 1.0f;

    for (char c : displayText) {
        if (c == '\n') {
            cursorX = x;
            cursorY += charHeight + 2 * scale;
            continue;
        }

        if (c < 32 || c > 127) c = '?';  // Replace non-printable with ?

        int charIdx = c - 32;
        float texX = (charIdx % 16) * texCharWidth;
        float texY = (charIdx / 16) * texCharHeight;

        // Two triangles per character (6 vertices)
        // Triangle 1: top-left, top-right, bottom-left
        // Triangle 2: top-right, bottom-right, bottom-left

        // Top-left
        vertices.insert(vertices.end(), {cursorX, cursorY, texX, texY, r, g, b, a});
        // Top-right
        vertices.insert(vertices.end(), {cursorX + charWidth, cursorY, texX + texCharWidth, texY, r, g, b, a});
        // Bottom-left
        vertices.insert(vertices.end(), {cursorX, cursorY + charHeight, texX, texY + texCharHeight, r, g, b, a});

        // Top-right
        vertices.insert(vertices.end(), {cursorX + charWidth, cursorY, texX + texCharWidth, texY, r, g, b, a});
        // Bottom-right
        vertices.insert(vertices.end(), {cursorX + charWidth, cursorY + charHeight, texX + texCharWidth, texY + texCharHeight, r, g, b, a});
        // Bottom-left
        vertices.insert(vertices.end(), {cursorX, cursorY + charHeight, texX, texY + texCharHeight, r, g, b, a});

        cursorX += charWidth;
    }

    if (vertices.empty()) return;

    // Upload vertex data
    wgpuQueueWriteBuffer(m_queue, m_textVertexBuffer, 0, vertices.data(), vertices.size() * sizeof(float));

    // Update screen size uniform
    setScreenSize(m_screenWidth, m_screenHeight);

    // Draw
    wgpuRenderPassEncoderSetPipeline(pass, m_textPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_textBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_textVertexBuffer, 0, vertices.size() * sizeof(float));
    wgpuRenderPassEncoderDraw(pass, (uint32_t)(vertices.size() / 8), 1, 0, 0);
}

} // namespace vivid
