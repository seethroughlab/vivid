#pragma once

// Vivid SIMD backend configuration.
// This header is runtime-internal — it MUST NOT appear in src/operator_api/.

#ifdef VIVID_HAS_HIGHWAY
#include "hwy/highway.h"
#include "hwy/aligned_allocator.h"
#define VIVID_SIMD_ENABLED 1
#else
#define VIVID_SIMD_ENABLED 0
#endif

#ifdef VIVID_HAS_ACCELERATE
#include <Accelerate/Accelerate.h>
#define VIVID_ACCELERATE_ENABLED 1
#else
#define VIVID_ACCELERATE_ENABLED 0
#endif
