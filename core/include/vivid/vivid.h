#pragma once

// Vivid - Main header
// Include this in your chain.cpp

#include <vivid/context.h>
#include <vivid/operator.h>
#include <vivid/chain.h>

namespace vivid {

// Chain entry points - implemented by user's chain.cpp
using SetupFn = void(*)(Context&);
using UpdateFn = void(*)(Context&);

// Platform-specific export macro for DLL symbols
#ifdef _WIN32
    #define VIVID_EXPORT __declspec(dllexport)
#else
    #define VIVID_EXPORT
#endif

// Macro for chain.cpp to export entry points
#define VIVID_CHAIN(setup_fn, update_fn) \
    extern "C" { \
        VIVID_EXPORT void vivid_setup(vivid::Context& ctx) { setup_fn(ctx); } \
        VIVID_EXPORT void vivid_update(vivid::Context& ctx) { update_fn(ctx); } \
    }

} // namespace vivid
