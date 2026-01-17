#include <vivid/audio/euclidean.h>
#include <vivid/audio/clock.h>
#include <vivid/audio/sequencer.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <vivid/viz_helpers.h>
#include <algorithm>
#include <cstdio>

namespace vivid::audio {

REGISTER_OPERATOR(Euclidean, "Audio Sequencing", "Euclidean rhythm pattern generator", false);

void Euclidean::init(Context& ctx) {
    if (!beginInit()) return;

    // Allocate minimal output buffer (Euclidean doesn't produce audio samples)
    allocateOutput(256, 2, AUDIO_SAMPLE_RATE);

    // Regenerate pattern
    regenerate();

    // Reset playback position
    m_currentStep.store(-1, std::memory_order_relaxed);
    m_triggeredFlag.store(false, std::memory_order_relaxed);
    m_visualTriggeredFlag.store(false, std::memory_order_relaxed);
    m_pendingTrigger = false;

    // Sync with trigger source's current count to avoid triggering all past beats
    Operator* trigSource = triggerSource();
    if (trigSource) {
        if (auto* clock = dynamic_cast<Clock*>(trigSource)) {
            m_lastTriggerCount = clock->triggerCount();
        }
    } else {
        m_lastTriggerCount = 0;
    }
}

void Euclidean::process(Context& ctx) {
    // Main thread process() is a no-op for Euclidean
    // All timing happens in generateBlock() on the audio thread
}

void Euclidean::generateBlock(uint32_t frameCount) {
    // Called on audio thread each block

    // Clear the triggered flag at the start of each block
    // This ensures triggered() only returns true for one block after advancing
    m_triggeredFlag.store(false, std::memory_order_release);

    // Track if ANY step during catchup was a hit (for trigger propagation)
    bool anyStepActive = false;

    // Check if our trigger source (e.g., Clock) has triggered
    // This allows Euclidean to advance automatically on the audio thread
    Operator* trigSource = triggerSource();
    if (trigSource) {
        // Try Clock first (most common trigger source)
        if (auto* clock = dynamic_cast<Clock*>(trigSource)) {
            uint64_t currentCount = clock->triggerCount();
            if (currentCount > m_lastTriggerCount) {
                // Clock has triggered - advance for each trigger we missed
                // Important: accumulate triggers so we don't lose active steps during catchup
                uint64_t triggers = currentCount - m_lastTriggerCount;
                for (uint64_t i = 0; i < triggers; ++i) {
                    advanceInternalNoFlag();  // Advance without setting flag
                    int current = m_currentStep.load(std::memory_order_relaxed);
                    int rot = static_cast<int>(rotation);
                    int numSteps = std::max(2, std::min(static_cast<int>(steps), MAX_STEPS));
                    int patternIdx = (current + rot) % numSteps;
                    if (m_pattern[patternIdx]) {
                        anyStepActive = true;
                    }
                }
                m_lastTriggerCount = currentCount;
            }
        }
        // Try Sequencer
        else if (auto* seq = dynamic_cast<Sequencer*>(trigSource)) {
            if (seq->triggered()) {
                advanceInternalNoFlag();
                int current = m_currentStep.load(std::memory_order_relaxed);
                int rot = static_cast<int>(rotation);
                int numSteps = std::max(2, std::min(static_cast<int>(steps), MAX_STEPS));
                int patternIdx = (current + rot) % numSteps;
                if (m_pattern[patternIdx]) {
                    anyStepActive = true;
                }
            }
        }
    }

    // If we have a pending trigger (from onTrigger or external trigger() call), advance
    if (m_pendingTrigger) {
        m_pendingTrigger = false;
        advanceInternalNoFlag();
        int current = m_currentStep.load(std::memory_order_relaxed);
        int rot = static_cast<int>(rotation);
        int numSteps = std::max(2, std::min(static_cast<int>(steps), MAX_STEPS));
        int patternIdx = (current + rot) % numSteps;
        if (m_pattern[patternIdx]) {
            anyStepActive = true;
        }
    }

    // Set triggered flags if ANY step was a hit during this block
    if (anyStepActive) {
        m_triggeredFlag.store(true, std::memory_order_release);       // For audio thread
        m_visualTriggeredFlag.store(true, std::memory_order_release); // For main thread
        if (m_onTrigger) {
            m_onTrigger();
        }
    }

    // Resize output buffer if needed (even though we don't produce audio)
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }
}

void Euclidean::midiNoteOn(uint8_t /*note*/, float /*velocity*/, uint8_t /*channel*/) {
    // MIDI note-on advances the step (same as trigger)
    // If our trigger source is a Clock, we poll triggerCount internally
    // in generateBlock(), so ignore external trigger events to avoid double-advancing
    Operator* src = triggerSource();
    if (src && dynamic_cast<Clock*>(src)) {
        return;  // Clock timing handled internally via triggerCount polling
    }

    // For non-Clock trigger sources, set pending flag
    // We'll advance in generateBlock() to ensure the triggered flag
    // is set at a consistent point in the block
    m_pendingTrigger = true;
}

void Euclidean::midiNoteOff(uint8_t /*note*/, float /*velocity*/, uint8_t /*channel*/) {
    // Nothing to do on note-off for step advance
}

void Euclidean::cleanup() {
    resetInit();
    releaseOutput();
}

void Euclidean::advanceInternalNoFlag() {
    // Called on audio thread - advances to next step WITHOUT setting triggered flag
    // Used during catchup to avoid overwriting flag for intermediate steps

    // Check if params changed and regenerate if needed
    int stepsVal = static_cast<int>(steps);
    int hitsVal = static_cast<int>(hits);
    if (stepsVal != m_cachedSteps || hitsVal != m_cachedHits) {
        m_cachedSteps = stepsVal;
        m_cachedHits = hitsVal;
        regenerate();
    }

    int numSteps = stepsVal;
    if (numSteps < 2) numSteps = 2;
    if (numSteps > MAX_STEPS) numSteps = MAX_STEPS;

    // Move to next step
    int current = m_currentStep.load(std::memory_order_relaxed);
    current = (current + 1) % numSteps;
    m_currentStep.store(current, std::memory_order_relaxed);
}

void Euclidean::advanceInternal() {
    // Called on audio thread - advances to next step and sets flag
    advanceInternalNoFlag();

    int numSteps = std::max(2, std::min(static_cast<int>(steps), MAX_STEPS));
    int rot = static_cast<int>(rotation);
    int current = m_currentStep.load(std::memory_order_relaxed);
    int patternIndex = (current + rot) % numSteps;

    bool stepActive = m_pattern[patternIndex];
    m_triggeredFlag.store(stepActive, std::memory_order_release);
    m_visualTriggeredFlag.store(stepActive, std::memory_order_release);

    if (stepActive && m_onTrigger) {
        m_onTrigger();
    }
}

void Euclidean::advance() {
    // Backward-compatible advance: directly advance and set flag
    // This matches the original synchronous behavior for main-thread callers

    // Check if params changed and regenerate if needed
    int stepsVal = static_cast<int>(steps);
    int hitsVal = static_cast<int>(hits);
    if (stepsVal != m_cachedSteps || hitsVal != m_cachedHits) {
        m_cachedSteps = stepsVal;
        m_cachedHits = hitsVal;
        regenerate();
    }

    int numSteps = stepsVal;
    int rot = static_cast<int>(rotation);

    if (numSteps < 2) numSteps = 2;
    if (numSteps > MAX_STEPS) numSteps = MAX_STEPS;

    // Move to next step
    int current = m_currentStep.load(std::memory_order_relaxed);
    current = (current + 1) % numSteps;
    m_currentStep.store(current, std::memory_order_relaxed);

    // Apply rotation and check pattern
    int rotatedStep = (current + rot) % numSteps;
    bool stepActive = m_pattern[rotatedStep];

    m_triggeredFlag.store(stepActive, std::memory_order_release);
    m_visualTriggeredFlag.store(stepActive, std::memory_order_release);
}

void Euclidean::reset() {
    m_currentStep.store(-1, std::memory_order_relaxed);  // So first advance() goes to step 0
    m_triggeredFlag.store(false, std::memory_order_relaxed);
    m_visualTriggeredFlag.store(false, std::memory_order_relaxed);
    m_pendingTrigger = false;
    m_lastTriggerCount = 0;
}

uint16_t Euclidean::pattern() const {
    int numSteps = static_cast<int>(steps);
    uint16_t result = 0;
    for (int i = 0; i < numSteps && i < 16; ++i) {
        if (m_pattern[i]) {
            result |= (1 << i);
        }
    }
    return result;
}

void Euclidean::regenerate() {
    // Bjorklund's algorithm for Euclidean rhythms
    int n = static_cast<int>(steps);
    int k = static_cast<int>(hits);

    // Clamp values
    n = std::max(2, std::min(n, MAX_STEPS));
    k = std::max(0, std::min(k, n));

    // Clear pattern
    for (int i = 0; i < MAX_STEPS; ++i) {
        m_pattern[i] = false;
    }

    if (k == 0) return;
    if (k >= n) {
        // All steps are hits
        for (int i = 0; i < n; ++i) {
            m_pattern[i] = true;
        }
        return;
    }

    // Bjorklund's algorithm
    // Build sequences of 0s and 1s, then interleave
    std::vector<std::vector<bool>> sequences;

    // Start with k sequences of [1] and (n-k) sequences of [0]
    for (int i = 0; i < k; ++i) {
        sequences.push_back({true});
    }
    for (int i = 0; i < n - k; ++i) {
        sequences.push_back({false});
    }

    // Repeatedly distribute remainder sequences
    while (true) {
        // Count sequences of different lengths
        size_t minLen = sequences[0].size();
        int numMin = 0;
        for (const auto& seq : sequences) {
            if (seq.size() == minLen) numMin++;
        }

        // Find sequences longer than minimum
        int numLonger = static_cast<int>(sequences.size()) - numMin;
        if (numLonger == 0 || numMin <= 1) break;

        // Take shorter sequences from the end and append to longer sequences
        int toDistribute = std::min(numMin, numLonger);

        for (int i = 0; i < toDistribute; ++i) {
            auto& shorter = sequences.back();
            auto& longer = sequences[i];

            for (bool b : shorter) {
                longer.push_back(b);
            }
            sequences.pop_back();
        }
    }

    // Flatten sequences into pattern
    int idx = 0;
    for (const auto& seq : sequences) {
        for (bool b : seq) {
            if (idx < MAX_STEPS) {
                m_pattern[idx++] = b;
            }
        }
    }
}

bool Euclidean::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    viz.drawBackground(bounds);

    int numSteps = static_cast<int>(steps);
    int numHits = static_cast<int>(hits);
    int rot = static_cast<int>(rotation);

    if (numSteps < 2) numSteps = 2;
    if (numSteps > MAX_STEPS) numSteps = MAX_STEPS;

    // Draw step circles
    float padding = 8.0f;
    float availableWidth = bounds.w - padding * 2;
    float stepWidth = availableWidth / numSteps;
    float radius = std::min(stepWidth * 0.35f, 5.0f);
    float circleY = bounds.cy() - 4;
    float startX = bounds.x + padding + stepWidth * 0.5f;

    // Get current state from atomic variables
    int currentStepVal = m_currentStep.load(std::memory_order_relaxed);
    bool isTriggered = m_triggeredFlag.load(std::memory_order_relaxed);

    for (int i = 0; i < numSteps; ++i) {
        float x = startX + i * stepWidth;

        // Apply rotation to get pattern index
        int patternIdx = (i + rot) % numSteps;
        bool isHit = m_pattern[patternIdx];
        bool isCurrent = (i == currentStepVal);

        // Color based on state
        uint32_t color;
        if (isCurrent && isTriggered) {
            color = VIZ_COL32(255, 200, 100, 255);  // Gold flash on trigger
        } else if (isCurrent) {
            color = VIZ_COL32(100, 200, 255, 255);  // Blue for current step
        } else if (isHit) {
            color = VIZ_COL32(200, 150, 255, 255);  // Purple for hit
        } else {
            color = VIZ_COL32(60, 60, 80, 255);     // Dark for rest
        }

        if (isHit) {
            dl->AddCircleFilled({x, circleY}, radius, color);
        } else {
            dl->AddCircle({x, circleY}, radius, color, 0, 1.5f);
        }
    }

    // Draw current step indicator (triangle below)
    if (currentStepVal >= 0 && currentStepVal < numSteps) {
        float markerX = startX + currentStepVal * stepWidth;
        float markerY = circleY + radius + 4;
        float triSize = 4.0f;
        dl->AddTriangleFilled(
            {markerX, markerY},
            {markerX - triSize, markerY + triSize * 1.5f},
            {markerX + triSize, markerY + triSize * 1.5f},
            VIZ_COL32(255, 200, 100, 255)
        );
    }

    // Draw E(k,n) notation at bottom
    char label[32];
    snprintf(label, sizeof(label), "E(%d,%d)", numHits, numSteps);
    VizBounds labelBounds = bounds.splitBottom(0.25f);
    viz.drawLabel(labelBounds, label, VizColors::TextSecondary);

    return true;
}

} // namespace vivid::audio
