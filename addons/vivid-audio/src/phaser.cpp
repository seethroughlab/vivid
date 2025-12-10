#include <vivid/audio/phaser.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Phaser::initEffect(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;

    m_lfoL.init(m_sampleRate, m_rateHz, dsp::LFOWaveform::Sine);
    m_lfoR.init(m_sampleRate, m_rateHz, dsp::LFOWaveform::Sine);

    m_feedbackL = 0.0f;
    m_feedbackR = 0.0f;

    // Reset all all-pass filters
    for (int i = 0; i < MAX_STAGES; ++i) {
        m_allpassL[i].reset();
        m_allpassR[i].reset();
    }
}

void Phaser::processEffect(const float* input, float* output, uint32_t frames) {
    float freqRange = MAX_FREQ - MIN_FREQ;

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Get LFO values (range 0 to 1)
        float lfoL = (m_lfoL.process() + 1.0f) * 0.5f;
        float lfoR = (m_lfoR.process() + 1.0f) * 0.5f;

        // Calculate modulated cutoff frequency
        float freqL = MIN_FREQ + lfoL * freqRange * m_depth;
        float freqR = MIN_FREQ + lfoR * freqRange * m_depth;

        // Add feedback
        float processL = inL + m_feedbackL * m_feedback;
        float processR = inR + m_feedbackR * m_feedback;

        // Process through all-pass filter chain
        for (int s = 0; s < m_stages; ++s) {
            m_allpassL[s].setCutoff(m_sampleRate, freqL);
            m_allpassR[s].setCutoff(m_sampleRate, freqR);
            processL = m_allpassL[s].process(processL);
            processR = m_allpassR[s].process(processR);
        }

        // Store for feedback
        m_feedbackL = processL;
        m_feedbackR = processR;

        // Output is the processed signal
        output[i * 2] = processL;
        output[i * 2 + 1] = processR;
    }
}

void Phaser::cleanupEffect() {
    for (int i = 0; i < MAX_STAGES; ++i) {
        m_allpassL[i].reset();
        m_allpassR[i].reset();
    }
    m_lfoL.reset();
    m_lfoR.reset();
    m_feedbackL = 0.0f;
    m_feedbackR = 0.0f;
}

} // namespace vivid::audio
