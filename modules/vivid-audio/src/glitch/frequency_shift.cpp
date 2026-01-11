#include <vivid/audio/glitch/frequency_shift.h>
#include <vivid/operator_registry.h>
#include <cmath>

namespace vivid::audio {

REGISTER_OPERATOR_EX(FrequencyShift, "Glitch", "Bode frequency shifter for metallic/robotic sounds", false, vivid::OutputKind::Audio);

static constexpr double PI = 3.14159265358979323846;
static constexpr double TWO_PI = 2.0 * PI;

void FrequencyShift::initHilbert() {
    // Generate Hilbert transform FIR coefficients
    // Using windowed sinc method with Blackman window
    for (int i = 0; i < HILBERT_TAPS; ++i) {
        int n = i - HILBERT_DELAY;

        if (n == 0) {
            // Center tap is 0 for Hilbert transform
            m_hilbertCoeffs[i] = 0.0f;
        } else if (n % 2 == 0) {
            // Even taps are 0
            m_hilbertCoeffs[i] = 0.0f;
        } else {
            // Odd taps: 2/(pi*n)
            float h = 2.0f / (PI * n);

            // Apply Blackman window
            float w = 0.42f - 0.5f * std::cos(TWO_PI * i / (HILBERT_TAPS - 1))
                           + 0.08f * std::cos(2.0 * TWO_PI * i / (HILBERT_TAPS - 1));

            m_hilbertCoeffs[i] = h * w;
        }
    }

    // Clear delay lines
    m_delayLineL.fill(0.0f);
    m_delayLineR.fill(0.0f);
    m_delayIndex = 0;
}

void FrequencyShift::initEffect(Context& ctx) {
    m_oscPhase = 0.0;
    m_lfoPhase = 0.0;
    m_delayLineL.fill(0.0f);
    m_delayLineR.fill(0.0f);
    m_delayIndex = 0;
}

void FrequencyShift::processHilbert(float inL, float inR,
                                     float& iL, float& qL,
                                     float& iR, float& qR) {
    // Store input in delay line
    m_delayLineL[m_delayIndex] = inL;
    m_delayLineR[m_delayIndex] = inR;

    // Compute Hilbert transform (quadrature component)
    qL = 0.0f;
    qR = 0.0f;

    for (int i = 0; i < HILBERT_TAPS; ++i) {
        int idx = (m_delayIndex - i + HILBERT_TAPS) % HILBERT_TAPS;
        qL += m_delayLineL[idx] * m_hilbertCoeffs[i];
        qR += m_delayLineR[idx] * m_hilbertCoeffs[i];
    }

    // In-phase component is delayed input (center of filter)
    int delayIdx = (m_delayIndex - HILBERT_DELAY + HILBERT_TAPS) % HILBERT_TAPS;
    iL = m_delayLineL[delayIdx];
    iR = m_delayLineR[delayIdx];

    // Advance delay line index
    m_delayIndex = (m_delayIndex + 1) % HILBERT_TAPS;
}

void FrequencyShift::processEffect(const float* input, float* output, uint32_t frames) {
    float currentBpm = static_cast<float>(bpm);
    uint32_t sampleRate = 48000;  // Standard sample rate

    // LFO rate
    float lfoHz = divisionToHz(m_modDiv, currentBpm);
    double lfoPhaseInc = lfoHz / sampleRate;

    // Oscillator phase increment per sample (will be modulated)
    double baseShift = static_cast<double>(shift);
    double modAmount = static_cast<double>(modDepth);

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Get analytic signal (I/Q components)
        float iL, qL, iR, qR;
        processHilbert(inL, inR, iL, qL, iR, qR);

        // Calculate modulated shift amount
        float lfo = std::sin(m_lfoPhase * TWO_PI);
        double currentShift = baseShift + modAmount * lfo;

        // Calculate oscillator
        double oscI = std::cos(m_oscPhase * TWO_PI);
        double oscQ = std::sin(m_oscPhase * TWO_PI);

        // Frequency shift: multiply analytic signal by complex oscillator
        // Up-shift: I*cos - Q*sin
        // Down-shift: I*cos + Q*sin
        // We use sign of shift to determine direction
        float outL, outR;
        if (currentShift >= 0) {
            outL = static_cast<float>(iL * oscI - qL * oscQ);
            outR = static_cast<float>(iR * oscI - qR * oscQ);
        } else {
            outL = static_cast<float>(iL * oscI + qL * oscQ);
            outR = static_cast<float>(iR * oscI + qR * oscQ);
        }

        // Advance oscillator phase
        double oscPhaseInc = std::abs(currentShift) / sampleRate;
        m_oscPhase += oscPhaseInc;
        if (m_oscPhase >= 1.0) m_oscPhase -= 1.0;

        // Advance LFO phase
        m_lfoPhase += lfoPhaseInc;
        if (m_lfoPhase >= 1.0) m_lfoPhase -= 1.0;

        // Apply mix
        float mixAmt = static_cast<float>(this->mix);
        output[i * 2] = inL * (1.0f - mixAmt) + outL * mixAmt;
        output[i * 2 + 1] = inR * (1.0f - mixAmt) + outR * mixAmt;
    }
}

} // namespace vivid::audio
