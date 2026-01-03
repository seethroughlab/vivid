#include <vivid/audio/euclidean.h>
#include <vivid/context.h>
#include <algorithm>

namespace vivid::audio {

void Euclidean::init(Context& ctx) {
    regenerate();
    m_initialized = true;
}

void Euclidean::process(Context& ctx) {
    // Euclidean is advanced externally via advance()
}

void Euclidean::cleanup() {
    m_initialized = false;
}

void Euclidean::advance() {
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
    m_currentStep = (m_currentStep + 1) % numSteps;

    // Apply rotation to get actual pattern index
    int patternIndex = (m_currentStep + rot) % numSteps;

    // Check if current step is a hit
    m_triggered = m_pattern[patternIndex];

    // Fire callback if triggered
    if (m_triggered && m_onTrigger) {
        m_onTrigger();
    }
}

void Euclidean::reset() {
    m_currentStep = -1;  // So first advance() goes to step 0
    m_triggered = false;
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

} // namespace vivid::audio
