#pragma once

#include "operator_api/types.h"

#include <cstdint>

namespace vivid {

inline float audio_scalar_sample(const VividAudioContext* ctx, uint32_t port_idx, uint32_t sample_idx) {
    if (!ctx || !ctx->input_buffers || !ctx->input_buffers[port_idx] || sample_idx >= ctx->buffer_size) {
        return 0.0f;
    }
    return ctx->input_buffers[port_idx][sample_idx];
}

inline float audio_scalar_block_start(const VividAudioContext* ctx, uint32_t port_idx) {
    return audio_scalar_sample(ctx, port_idx, 0);
}

inline float audio_scalar_last(const VividAudioContext* ctx, uint32_t port_idx) {
    if (!ctx || ctx->buffer_size == 0) {
        return 0.0f;
    }
    return audio_scalar_sample(ctx, port_idx, ctx->buffer_size - 1);
}

}  // namespace vivid
