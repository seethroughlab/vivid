#include <vivid/audio/clock.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <vivid/viz_helpers.h>
#include <cmath>
#include <cstdio>

namespace vivid::audio {

REGISTER_OPERATOR_EX(Clock, "Audio Sequencing", "Master tempo clock with beat/bar triggers", false, vivid::OutputKind::Audio);

void Clock::init(Context& ctx) {
    if (!beginInit()) return;

    // Allocate minimal output buffer (Clock doesn't produce audio, just triggers)
    allocateOutput(256, 2, SAMPLE_RATE);

    reset();
}

void Clock::process(Context& ctx) {
    // Main thread process() is a no-op for Clock
    // All timing happens in generateBlock() on the audio thread
}

void Clock::generateBlock(uint32_t frameCount) {
    // Called on audio thread - sample-accurate timing

    // NOTE: We do NOT clear m_triggeredFlag here. The flag is cleared by the main
    // thread when it calls triggered() (which uses exchange). Audio-thread consumers
    // (like Sequencer) use triggerCount() polling instead of the flag.

    if (!m_running.load(std::memory_order_relaxed)) {
        return;
    }

    // Get parameters (these are set from main thread, read here)
    float currentBpm = static_cast<float>(bpm);
    float swingAmt = m_swingEnabled ? static_cast<float>(swing) : 0.0f;

    // Calculate phase increment per sample
    float beatsPerSecond = currentBpm / 60.0f;
    float divMultiplier = getDivisionMultiplier();
    float triggersPerSecond = beatsPerSecond * divMultiplier;
    double phaseIncPerSample = static_cast<double>(triggersPerSecond) / static_cast<double>(SAMPLE_RATE);

    // Process each sample for precise trigger timing
    for (uint32_t i = 0; i < frameCount; ++i) {
        double oldPhase = m_phase;
        m_phase += phaseIncPerSample;

        // Detect phase wrap (trigger point)
        if (m_phase >= 1.0) {
            m_phase -= 1.0;

            // Apply swing to even beats
            uint64_t count = m_triggerCount.load(std::memory_order_relaxed);
            bool isOddBeat = (count % 2) == 1;

            if (!isOddBeat || swingAmt == 0.0f) {
                // Trigger on this sample (even beats, or any beat when swing is 0)
                m_triggeredFlag.store(true, std::memory_order_release);
                m_triggerCount.fetch_add(1, std::memory_order_relaxed);

                if (m_callback) {
                    m_callback();
                }
            } else {
                // Odd beat with swing: capture the swing delay NOW so it doesn't change
                // if the user moves the mouse before the threshold is crossed
                m_capturedSwingDelay = swingAmt * 0.33f;
            }
            m_lastTickOdd = isOddBeat;
        }

        // Handle swing delay (trigger odd beats late)
        // Use captured swing delay so changing swing mid-beat doesn't lose the trigger
        if (m_capturedSwingDelay > 0.0f && m_lastTickOdd) {
            if (oldPhase < m_capturedSwingDelay && m_phase >= m_capturedSwingDelay) {
                m_triggeredFlag.store(true, std::memory_order_release);
                m_triggerCount.fetch_add(1, std::memory_order_relaxed);

                if (m_callback) {
                    m_callback();
                }
                m_lastTickOdd = false;
                m_capturedSwingDelay = 0.0f;  // Reset after use
            }
        }
    }
}

void Clock::cleanup() {
    resetInit();
    releaseOutput();
}

void Clock::reset() {
    m_phase = 0.0;
    m_triggerCount.store(0, std::memory_order_relaxed);
    m_triggeredFlag.store(false, std::memory_order_relaxed);
    m_lastTickOdd = false;
    m_capturedSwingDelay = 0.0f;
    m_midiClockCount = 0;
    m_midiClockPhase = 0.0;
}

// -----------------------------------------------------------------------------
// MIDI Clock Sync
// -----------------------------------------------------------------------------

void Clock::midiClock() {
    if (!m_midiClockSync) return;

    // Calculate BPM from MIDI clock timing (24 PPQ)
    double now = static_cast<double>(m_triggerCount.load()) / 24.0;  // Rough time estimate

    if (m_midiClockCount > 0 && m_lastMidiClockTime > 0.0) {
        double interval = now - m_lastMidiClockTime;
        if (interval > 0.0001) {  // Avoid division by zero
            // Smooth the interval calculation
            m_midiClockInterval = m_midiClockInterval * 0.9 + interval * 0.1;

            // Convert 24 PPQ interval to BPM
            // BPM = 60 / (interval_per_tick * 24)
            float newBpm = 60.0f / static_cast<float>(m_midiClockInterval * 24.0);
            newBpm = std::clamp(newBpm, 20.0f, 300.0f);
            bpm = newBpm;
        }
    }

    m_lastMidiClockTime = now;
    m_midiClockCount++;

    // Trigger on every 24th tick (quarter note at 24 PPQ)
    if (m_midiClockCount % 24 == 0) {
        m_triggeredFlag.store(true, std::memory_order_release);
        m_triggerCount.fetch_add(1, std::memory_order_relaxed);
        if (m_callback) {
            m_callback();
        }
    }
}

void Clock::midiStart() {
    reset();
    start();
}

void Clock::midiStop() {
    stop();
}

void Clock::midiContinue() {
    start();
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

bool Clock::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    viz.drawBackground(bounds);

    // Draw beat grid (4 beats per bar)
    constexpr int BEATS_PER_BAR = 4;
    float dotSpacing = (bounds.w - 16) / (BEATS_PER_BAR - 1);
    float dotY = bounds.cy() - 8;
    float startX = bounds.x + 8;

    uint32_t currentBeat = beat();
    bool running = m_running.load(std::memory_order_relaxed);
    bool triggered = m_triggeredFlag.load(std::memory_order_relaxed);

    for (int i = 0; i < BEATS_PER_BAR; ++i) {
        float x = startX + i * dotSpacing;
        float radius = 5.0f;

        bool isCurrent = (i == static_cast<int>(currentBeat)) && running;
        bool isDownbeat = (i == 0);

        // Color based on state
        uint32_t color;
        if (isCurrent && triggered) {
            color = VIZ_COL32(255, 200, 100, 255);  // Gold flash on trigger
        } else if (isCurrent) {
            color = VIZ_COL32(100, 200, 255, 255);  // Blue for current beat
        } else if (isDownbeat) {
            color = VIZ_COL32(150, 150, 170, 255);  // Lighter for downbeat
        } else {
            color = VIZ_COL32(80, 80, 100, 255);    // Dim for other beats
        }

        dl->AddCircleFilled({x, dotY}, radius, color);

        // Draw outline on downbeat
        if (isDownbeat) {
            dl->AddCircle({x, dotY}, radius + 1.0f, VIZ_COL32(180, 180, 200, 200), 0, 1.0f);
        }
    }

    // Draw bar indicator at left
    uint32_t barNum = bar();
    char barStr[16];
    snprintf(barStr, sizeof(barStr), "%u", barNum + 1);
    dl->AddText({bounds.x + 2, bounds.y + 12}, VizColors::TextDim, barStr);

    // Draw BPM and status at bottom
    char label[32];
    snprintf(label, sizeof(label), "%.0f BPM", static_cast<float>(bpm));
    VizBounds labelBounds = bounds.splitBottom(0.25f);
    viz.drawLabel(labelBounds, label, running ? VizColors::TextSecondary : VizColors::TextDim);

    return true;
}

InspectData Clock::inspect() const {
    auto data = Operator::inspect();
    data.set("is_running", isRunning() ? 1.0f : 0.0f);
    data.set("trigger_count", static_cast<float>(triggerCount()));
    data.set("beat", static_cast<float>(beat()));
    data.set("bar", static_cast<float>(bar()));
    return data;
}

} // namespace vivid::audio
