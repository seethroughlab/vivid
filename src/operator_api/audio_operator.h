#pragma once
#include "operator_api/types.h"

struct VividAudioState {
    float**   input_buffers;    // [port_idx][sample_idx]
    float**   output_buffers;   // [port_idx][sample_idx]
    uint32_t  buffer_size;      // samples per callback (256)
    uint32_t  sample_rate;      // 48000
};

static inline VividAudioState* vivid_audio(const VividProcessContext* ctx) {
    return static_cast<VividAudioState*>(ctx->audio);
}
