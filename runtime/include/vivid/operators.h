#pragma once

// Include all built-in operators

// Phase 2: Core operators
#include "vivid/operators/solid_color.h"
#include "vivid/operators/noise.h"
#include "vivid/operators/composite.h"
#include "vivid/operators/blur.h"
#include "vivid/operators/output.h"

// Phase 3: Additional 2D operators
#include "vivid/operators/passthrough.h"
#include "vivid/operators/gradient.h"
#include "vivid/operators/brightness_contrast.h"
#include "vivid/operators/hsv.h"
#include "vivid/operators/transform.h"
#include "vivid/operators/feedback.h"
#include "vivid/operators/edge_detect.h"
#include "vivid/operators/displacement.h"
#include "vivid/operators/chromatic_aberration.h"
#include "vivid/operators/pixelate.h"
#include "vivid/operators/mirror.h"

// Phase 4: 3D operators
#include "vivid/operators/render3d.h"
