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

// Macro to export chain entry points
#define VIVID_CHAIN(setup_fn, update_fn) \
    extern "C" { \
        void vivid_setup(vivid::Chain& chain) { setup_fn(chain); } \
        void vivid_update(vivid::Chain& chain, vivid::Context& ctx) { update_fn(chain, ctx); } \
    }
