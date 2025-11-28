#pragma once

// Vivid - Creative Coding Framework
// Main include file for Vivid projects

// Version information
#define VIVID_VERSION_MAJOR 0
#define VIVID_VERSION_MINOR 1
#define VIVID_VERSION_PATCH 0

#include "types.h"
#include "context.h"
#include "operator.h"
#include "node_macro.h"
#include "params.h"

// User implements this function in their project
// Called once at startup and after each hot-reload
extern void setup(vivid::Context& ctx);
