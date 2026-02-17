#pragma once

/**
 * @file param_animator.h
 * @brief Per-parameter ramp animations with easing
 *
 * ParamAnimator manages a set of active parameter ramps that are ticked
 * each frame. Used by MCP param_ramp tool for animated parameter transitions.
 */

#include <vivid/easing.h>
#include <string>
#include <vector>

namespace vivid {

class Chain;

class ParamAnimator {
public:
    /// Start a new ramp. Replaces any existing ramp on the same op+param.
    void startRamp(const std::string& op, const std::string& param,
                   float from, float to, float duration, EasingCurve easing);

    /// Tick all active ramps. Call once per frame.
    void update(float dt, Chain& chain);

    /// Check if any ramps are active
    bool hasActiveRamps() const { return !m_ramps.empty(); }

private:
    struct ParamRamp {
        std::string op;
        std::string param;
        float from, to;
        float elapsed = 0.0f;
        float duration;
        EasingCurve easing;
    };

    std::vector<ParamRamp> m_ramps;
};

} // namespace vivid
