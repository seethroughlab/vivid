/**
 * @file trigger.cpp
 * @brief Trigger utility operator implementation
 */

#include <vivid/effects/trigger.h>
#include <algorithm>

namespace vivid::effects {

Trigger::Trigger() {
    registerParam(attack);
    registerParam(decay);
}

void Trigger::init(Context& /*ctx*/) {
    m_value = 0.0f;
    m_target = 0.0f;
}

void Trigger::fire(float intensity) {
    m_target = std::clamp(intensity, 0.0f, 1.0f);

    // If attack is zero, jump immediately to target
    if (static_cast<float>(attack) < 0.001f) {
        m_value = m_target;
    }
}

void Trigger::process(Context& /*ctx*/) {
    float attackVal = static_cast<float>(attack);
    float decayVal = static_cast<float>(decay);

    // Attack phase: ramp up to target
    if (m_value < m_target) {
        if (attackVal < 0.001f) {
            // Instant attack
            m_value = m_target;
        } else {
            // Smooth attack (higher attack = slower)
            float attackSpeed = 1.0f - attackVal * 0.9f;  // 0.1 to 1.0
            m_value += (m_target - m_value) * attackSpeed;

            // Snap to target when close
            if (m_target - m_value < 0.01f) {
                m_value = m_target;
            }
        }
    }

    // Decay phase: exponential decay
    if (m_value >= m_target) {
        m_target = 0.0f;  // Reset target after reaching it
        m_value *= decayVal;

        // Zero out when negligible
        if (m_value < 0.001f) {
            m_value = 0.0f;
        }
    }
}

} // namespace vivid::effects
