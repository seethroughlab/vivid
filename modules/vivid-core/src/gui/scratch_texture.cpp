#include <vivid/gui/scratch_texture.h>
#include <cstring>

namespace vivid {

// Helper to convert string to WGPUStringView
static WGPUStringView toStringView(const char* str) {
    return {str, strlen(str)};
}

ScratchTexture::~ScratchTexture() {
    release();
}

ScratchTexture::ScratchTexture(ScratchTexture&& other) noexcept
    : m_device(other.m_device)
    , m_queue(other.m_queue)
    , m_texture(other.m_texture)
    , m_view(other.m_view)
    , m_width(other.m_width)
    , m_height(other.m_height)
{
    other.m_device = nullptr;
    other.m_queue = nullptr;
    other.m_texture = nullptr;
    other.m_view = nullptr;
    other.m_width = 0;
    other.m_height = 0;
}

ScratchTexture& ScratchTexture::operator=(ScratchTexture&& other) noexcept {
    if (this != &other) {
        release();
        m_device = other.m_device;
        m_queue = other.m_queue;
        m_texture = other.m_texture;
        m_view = other.m_view;
        m_width = other.m_width;
        m_height = other.m_height;
        other.m_device = nullptr;
        other.m_queue = nullptr;
        other.m_texture = nullptr;
        other.m_view = nullptr;
        other.m_width = 0;
        other.m_height = 0;
    }
    return *this;
}

void ScratchTexture::init(WGPUDevice device, WGPUQueue queue) {
    m_device = device;
    m_queue = queue;
}

void ScratchTexture::release() {
    if (m_view) {
        wgpuTextureViewRelease(m_view);
        m_view = nullptr;
    }
    if (m_texture) {
        wgpuTextureRelease(m_texture);
        m_texture = nullptr;
    }
    m_width = 0;
    m_height = 0;
}

WGPUTextureView ScratchTexture::upload(const Operator::CpuPixelView& view) {
    if (!view.valid() || !m_device || !m_queue) return nullptr;

    int width = view.width;
    int height = view.height;

    // Recreate texture if size changed
    if (width != m_width || height != m_height) {
        release();

        WGPUTextureDescriptor desc{};
        desc.label = toStringView("cpu_pixel_scratch");
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        desc.dimension = WGPUTextureDimension_2D;
        desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        desc.format = WGPUTextureFormat_BGRA8Unorm;  // Match CPU BGRA format
        desc.mipLevelCount = 1;
        desc.sampleCount = 1;

        m_texture = wgpuDeviceCreateTexture(m_device, &desc);
        if (!m_texture) return nullptr;

        WGPUTextureViewDescriptor viewDesc{};
        viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.baseMipLevel = 0;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        viewDesc.aspect = WGPUTextureAspect_All;

        m_view = wgpuTextureCreateView(m_texture, &viewDesc);
        m_width = width;
        m_height = height;
    }

    // Upload pixels to texture
    WGPUTexelCopyTextureInfo dstInfo{};
    dstInfo.texture = m_texture;
    dstInfo.mipLevel = 0;
    dstInfo.origin = {0, 0, 0};
    dstInfo.aspect = WGPUTextureAspect_All;

    size_t rowStride = view.rowStride();
    WGPUTexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = static_cast<uint32_t>(rowStride);
    layout.rowsPerImage = static_cast<uint32_t>(height);

    WGPUExtent3D writeSize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    size_t dataSize = rowStride * height;

    wgpuQueueWriteTexture(m_queue, &dstInfo, view.data, dataSize, &layout, &writeSize);

    return m_view;
}

} // namespace vivid
