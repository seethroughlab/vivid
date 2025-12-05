#pragma once

// Vivid - LLM-first creative coding framework
// Main include file

#include "context.h"
#include "chain.h"
#include "operator.h"
#include "texture_utils.h"

namespace vivid {

// Version info
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 1;
constexpr int VERSION_PATCH = 0;

} // namespace vivid

// Macro to export chain entry points for hot reload
// Usage:
//   void setup(vivid::Context& ctx) { /* called once on load */ }
//   void update(vivid::Context& ctx) { /* called every frame */ }
//   VIVID_CHAIN(setup, update)

#if defined(_WIN32) || defined(_WIN64)
    #define VIVID_EXPORT __declspec(dllexport)
#else
    #define VIVID_EXPORT __attribute__((visibility("default")))
#endif

#define VIVID_CHAIN(setup_fn, update_fn) \
    extern "C" { \
        VIVID_EXPORT void vivid_setup(vivid::Context& ctx) { setup_fn(ctx); } \
        VIVID_EXPORT void vivid_update(vivid::Context& ctx) { update_fn(ctx); } \
    }
