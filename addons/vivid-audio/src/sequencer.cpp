#include <vivid/audio/sequencer.h>
#include <vivid/context.h>

namespace vivid::audio {

void Sequencer::init(Context& ctx) {
    // Don't clear pattern - it may have been set before init()
    // Just reset the playback position
    reset();
    m_initialized = true;
}

void Sequencer::process(Context& ctx) {
    // Sequencer is typically advanced externally via advance()
    // process() just clears the trigger flag each frame
    // (trigger is set in advance() and should be read before next process)
}

void Sequencer::cleanup() {
    m_initialized = false;
}

void Sequencer::setStep(int step, bool on, float velocity) {
    if (step >= 0 && step < MAX_STEPS) {
        m_pattern[step] = on;
        m_velocities[step] = velocity;
    }
}

bool Sequencer::getStep(int step) const {
    if (step >= 0 && step < MAX_STEPS) {
        return m_pattern[step];
    }
    return false;
}

float Sequencer::getVelocity(int step) const {
    if (step >= 0 && step < MAX_STEPS) {
        return m_velocities[step];
    }
    return 0.0f;
}

void Sequencer::clearPattern() {
    for (int i = 0; i < MAX_STEPS; ++i) {
        m_pattern[i] = false;
        m_velocities[i] = 1.0f;
    }
}

void Sequencer::setPattern(uint16_t pattern) {
    for (int i = 0; i < MAX_STEPS; ++i) {
        m_pattern[i] = (pattern & (1 << i)) != 0;
    }
}

void Sequencer::advance() {
    int numSteps = static_cast<int>(steps);
    if (numSteps < 1) numSteps = 1;
    if (numSteps > MAX_STEPS) numSteps = MAX_STEPS;

    // Move to next step
    m_currentStep = (m_currentStep + 1) % numSteps;

    // Check if current step is active
    m_triggered = m_pattern[m_currentStep];
    m_currentVelocity = m_triggered ? m_velocities[m_currentStep] : 0.0f;
}

void Sequencer::reset() {
    m_currentStep = -1;  // So first advance() goes to step 0
    m_triggered = false;
    m_currentVelocity = 0.0f;
}

} // namespace vivid::audio
