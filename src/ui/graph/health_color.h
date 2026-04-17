#pragma once

#include "runtime/core/runtime_health.h"

namespace vivid::ui {

// Color palette for the diagnostics-panel health pill and the matching
// status-bar dot. Kept here so the same mapping drives both surfaces and
// can be unit-tested without a Renderer2D / GPU context.

struct HealthRgb { float r, g, b; };

inline HealthRgb health_color(vivid::runtime_health::Severity s) {
    using S = vivid::runtime_health::Severity;
    switch (s) {
        case S::Ok:      return {0.30f, 0.85f, 0.40f}; // green
        case S::Warning: return {0.95f, 0.82f, 0.30f}; // amber
        case S::Error:   return {0.95f, 0.55f, 0.20f}; // orange
        case S::Fatal:   return {0.95f, 0.35f, 0.30f}; // red
    }
    return {0.30f, 0.85f, 0.40f};
}

}  // namespace vivid::ui
