#pragma once

// Vivid - Creative Coding Framework
// Main include file for Vivid projects

// Version information
#define VIVID_VERSION_MAJOR 0
#define VIVID_VERSION_MINOR 1
#define VIVID_VERSION_PATCH 0

// Core API
#include "types.h"
#include "context.h"
#include "operator.h"
#include "node_macro.h"
#include "params.h"
#include "param_ref.h"
#include "keys.h"

// Built-in Operators - Media
#include "operators/videofile.h"
#include "operators/imagefile.h"

// Built-in Operators - Generators
#include "operators/noise.h"
#include "operators/gradient.h"
#include "operators/shape.h"
#include "operators/constant.h"

// Built-in Operators - Modulation
#include "operators/lfo.h"
#include "operators/math.h"
#include "operators/logic.h"

// Built-in Operators - Effects
#include "operators/blur.h"
#include "operators/brightness.h"
#include "operators/hsv.h"
#include "operators/composite.h"
#include "operators/feedback.h"
#include "operators/transform.h"
#include "operators/displacement.h"
#include "operators/edge.h"

// Built-in Operators - Utility
#include "operators/switch.h"
#include "operators/passthrough.h"
#include "operators/reference.h"

// User implements this function in their project
// Called once at startup and after each hot-reload
extern void setup(vivid::Context& ctx);
