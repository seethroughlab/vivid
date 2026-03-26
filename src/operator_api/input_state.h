#pragma once
#include "operator_api/types.h"

static inline const VividInputState* vivid_input(const VividFrameContext* ctx) {
    return static_cast<const VividInputState*>(ctx->input);
}
