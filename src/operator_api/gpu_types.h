#pragma once
#include <webgpu/webgpu.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VividGpuBuffer {
    WGPUBuffer  buffer;
    uint64_t    size_bytes;
    uint32_t    usage;       // WGPUBufferUsage flags
    const char* label;       // may be null
} VividGpuBuffer;

typedef struct VividComputeBuffer {
    WGPUBuffer  buffer;
    uint64_t    size_bytes;
    uint32_t    element_count;
    uint32_t    element_stride;
    const char* label;
} VividComputeBuffer;

typedef struct VividVertexAttribute {
    WGPUVertexFormat format;
    uint64_t         offset;
    uint32_t         shader_location;
} VividVertexAttribute;

typedef struct VividMesh {
    WGPUBuffer              vertex_buffer;
    uint64_t                vertex_buffer_offset;
    uint32_t                vertex_count;
    uint32_t                vertex_stride;

    WGPUBuffer              index_buffer;   // nullable
    WGPUIndexFormat         index_format;
    uint32_t                index_count;    // 0 if no index buffer

    WGPUPrimitiveTopology   topology;

    const VividVertexAttribute* attributes;
    uint32_t                    attribute_count;
} VividMesh;

#ifdef __cplusplus
}
#endif
