#include <vivid/audio/clock.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Clock::init(Context& ctx) {
    m_sampleRate = 48000;  // Standard audio sample rate
    reset();
    m_initialized = true;
}

void Clock::process(Context& ctx) {
    if (!m_initialized || !m_running) {
        m_triggered = false;
        return;
    }

    // Calculate samples per beat based on BPM and division
    float beatsPerSecond = static_cast<float>(bpm) / 60.0f;
    float divMultiplier = getDivisionMultiplier();
    float triggersPerSecond = beatsPerSecond * divMultiplier;

    // Phase increment per frame (sample-accurate, not frame-rate dependent)
    double phaseInc = triggersPerSecond * (ctx.audioFramesThisFrame() / static_cast<double>(m_sampleRate));

    // Check for trigger
    double oldPhase = m_phase;
    m_phase += phaseInc;

    m_triggered = false;

    // Get swing amount
    float swingAmt = static_cast<float>(swing);

    // Detect phase wrap (trigger point)
    if (m_phase >= 1.0) {
        m_phase -= 1.0;

        // Apply swing to even beats
        bool isOddBeat = (m_triggerCount % 2) == 1;

        if (!isOddBeat || swingAmt == 0.0f) {
            m_triggered = true;
            m_triggerCount++;

            if (m_callback) {
                m_callback();
            }
        }
        m_lastTickOdd = isOddBeat;
    }

    // Handle swing delay (trigger odd beats late)
    if (swingAmt > 0.0f && m_lastTickOdd) {
        float swingDelay = swingAmt * 0.33f;  // Max 33% of beat
        if (oldPhase < swingDelay && m_phase >= swingDelay) {
            m_triggered = true;
            m_triggerCount++;

            if (m_callback) {
                m_callback();
            }
            m_lastTickOdd = false;
        }
    }
}

void Clock::cleanup() {
    m_initialized = false;
}

void Clock::reset() {
    m_phase = 0.0;
    m_triggerCount = 0;
    m_triggered = false;
    m_lastTickOdd = false;
}

float Clock::getDivisionMultiplier() const {
    switch (m_division) {
        case ClockDiv::Whole:          return 0.25f;
        case ClockDiv::Half:           return 0.5f;
        case ClockDiv::Quarter:        return 1.0f;
        case ClockDiv::Eighth:         return 2.0f;
        case ClockDiv::Sixteenth:      return 4.0f;
        case ClockDiv::ThirtySecond:   return 8.0f;
        case ClockDiv::DottedQuarter:  return 0.667f;
        case ClockDiv::DottedEighth:   return 1.333f;
        case ClockDiv::TripletQuarter: return 1.5f;
        case ClockDiv::TripletEighth:  return 3.0f;
        default:                       return 1.0f;
    }
}

} // namespace vivid::audio
