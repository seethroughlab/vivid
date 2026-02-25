#ifndef VIVID_OPERATOR_API_TYPES_H
#define VIVID_OPERATOR_API_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

typedef enum VividDomain {
    VIVID_DOMAIN_CONTROL = 0,
    VIVID_DOMAIN_AUDIO   = 1,
    VIVID_DOMAIN_GPU     = 2,
} VividDomain;

typedef enum VividParamType {
    VIVID_PARAM_FLOAT = 0,
    VIVID_PARAM_INT   = 1,
    VIVID_PARAM_BOOL  = 2,
} VividParamType;

typedef enum VividPortType {
    VIVID_PORT_CONTROL_FLOAT  = 0,
    VIVID_PORT_CONTROL_INT    = 1,
    VIVID_PORT_CONTROL_BOOL   = 2,
    VIVID_PORT_AUDIO_FLOAT    = 3,
    VIVID_PORT_CONTROL_SPREAD = 4,
} VividPortType;

typedef enum VividPortDirection {
    VIVID_PORT_INPUT  = 0,
    VIVID_PORT_OUTPUT = 1,
} VividPortDirection;

// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------

typedef struct VividParamDescriptor {
    const char*    name;
    VividParamType type;
    float          default_value;
    float          min_value;
    float          max_value;
} VividParamDescriptor;

typedef struct VividPortDescriptor {
    const char*       name;
    VividPortType     type;
    VividPortDirection direction;
} VividPortDescriptor;

typedef struct VividOperatorDescriptor {
    const char*               name;
    VividDomain               domain;
    uint32_t                  param_count;
    const VividParamDescriptor* params;
    uint32_t                  port_count;
    const VividPortDescriptor*  ports;
    int                       time_dependent;  // 1 if operator reads ctx->time, 0 otherwise
} VividOperatorDescriptor;

// ---------------------------------------------------------------------------
// Spread port — variable-length float array
// ---------------------------------------------------------------------------

typedef struct VividSpreadPort {
    float*   data;      // pointer to spread data
    uint32_t length;    // current number of floats
    uint32_t capacity;  // allocated size (for output ports)
} VividSpreadPort;

// ---------------------------------------------------------------------------
// Process context — passed each tick
// ---------------------------------------------------------------------------

typedef struct VividProcessContext {
    double    time;
    double    delta_time;
    uint64_t  frame;
    float*    param_values;   // indexed by param descriptor order
    float*    input_values;   // indexed by input port order (VIVID_PORT_INPUT only)
    float*    output_values;  // indexed by output port order (VIVID_PORT_OUTPUT only)
    void*     gpu;            // VividGpuState* for GPU operators, NULL otherwise
    void*     audio;          // VividAudioState* for audio operators, NULL otherwise
    VividSpreadPort* input_spreads;   // [input_port_idx], NULL if none
    VividSpreadPort* output_spreads;  // [output_port_idx], NULL if none
} VividProcessContext;

// ---------------------------------------------------------------------------
// Function pointer typedefs (dlopen entry points)
// ---------------------------------------------------------------------------

typedef const VividOperatorDescriptor* (*VividDescriptorFn)(void);
typedef void*  (*VividCreateFn)(void);
typedef void   (*VividDestroyFn)(void* instance);
typedef void   (*VividProcessFn)(void* instance, const VividProcessContext* ctx);

// ---------------------------------------------------------------------------
// Thumbnail context — optional custom thumbnail rendering
// ---------------------------------------------------------------------------

typedef struct VividThumbnailContext {
    uint8_t*  pixels;         // RGBA8 buffer (pre-allocated, row-major)
    uint32_t  width;          // 140
    uint32_t  height;         // 88
    uint32_t  stride;         // width * 4
    double    time;
    float*    output_values;
    uint32_t  output_count;
    float*    param_values;
    uint32_t  param_count;
} VividThumbnailContext;

typedef void (*VividDrawThumbnailFn)(void* instance, const VividThumbnailContext* ctx);

#ifdef __cplusplus
}
#endif

#endif // VIVID_OPERATOR_API_TYPES_H
