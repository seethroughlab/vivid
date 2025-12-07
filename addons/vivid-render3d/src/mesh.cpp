#include <vivid/render3d/mesh.h>
#include <vivid/context.h>

namespace vivid::render3d {

namespace {

inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

} // anonymous namespace

Mesh::~Mesh() {
    release();
}

Mesh::Mesh(Mesh&& other) noexcept
    : vertices(std::move(other.vertices))
    , indices(std::move(other.indices))
    , m_vertexBuffer(other.m_vertexBuffer)
    , m_indexBuffer(other.m_indexBuffer)
{
    other.m_vertexBuffer = nullptr;
    other.m_indexBuffer = nullptr;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        release();
        vertices = std::move(other.vertices);
        indices = std::move(other.indices);
        m_vertexBuffer = other.m_vertexBuffer;
        m_indexBuffer = other.m_indexBuffer;
        other.m_vertexBuffer = nullptr;
        other.m_indexBuffer = nullptr;
    }
    return *this;
}

void Mesh::upload(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Release existing buffers if any
    release();

    if (vertices.empty()) return;

    // Create vertex buffer
    WGPUBufferDescriptor vertexBufferDesc = {};
    vertexBufferDesc.label = toStringView("Mesh Vertex Buffer");
    vertexBufferDesc.size = vertices.size() * sizeof(Vertex3D);
    vertexBufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    vertexBufferDesc.mappedAtCreation = false;
    m_vertexBuffer = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);

    // Upload vertex data
    wgpuQueueWriteBuffer(ctx.queue(), m_vertexBuffer, 0,
                         vertices.data(), vertices.size() * sizeof(Vertex3D));

    // Create index buffer if we have indices
    if (!indices.empty()) {
        WGPUBufferDescriptor indexBufferDesc = {};
        indexBufferDesc.label = toStringView("Mesh Index Buffer");
        indexBufferDesc.size = indices.size() * sizeof(uint32_t);
        indexBufferDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
        indexBufferDesc.mappedAtCreation = false;
        m_indexBuffer = wgpuDeviceCreateBuffer(device, &indexBufferDesc);

        // Upload index data
        wgpuQueueWriteBuffer(ctx.queue(), m_indexBuffer, 0,
                             indices.data(), indices.size() * sizeof(uint32_t));
    }
}

void Mesh::release() {
    if (m_vertexBuffer) {
        wgpuBufferDestroy(m_vertexBuffer);
        wgpuBufferRelease(m_vertexBuffer);
        m_vertexBuffer = nullptr;
    }
    if (m_indexBuffer) {
        wgpuBufferDestroy(m_indexBuffer);
        wgpuBufferRelease(m_indexBuffer);
        m_indexBuffer = nullptr;
    }
}

bool Mesh::valid() const {
    return m_vertexBuffer != nullptr;
}

} // namespace vivid::render3d
