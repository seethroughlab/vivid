#include <vivid/audio/tape_effect.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void TapeEffect::initEffect(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;

    // Initialize delay lines for pitch modulation
    // Max delay: base + wow depth + flutter depth + margin
    uint32_t maxDelaySamples = static_cast<uint32_t>(
        (BASE_DELAY_MS + MAX_WOW_DEPTH_MS + MAX_FLUTTER_DEPTH_MS + 5.0f) *
        static_cast<float>(m_sampleRate) / 1000.0f
    );
    m_delayL.init(maxDelaySamples);
    m_delayR.init(maxDelaySamples);

    // Initialize wow LFOs (slow pitch drift, 0.5-2 Hz)
    // Slightly different frequencies for L/R to add stereo movement
    m_wowLfoL.init(m_sampleRate, 0.8f, dsp::LFOWaveform::Sine);
    m_wowLfoR.init(m_sampleRate, 0.75f, dsp::LFOWaveform::Sine);

    // Initialize flutter LFOs (fast jitter, 5-15 Hz)
    m_flutterLfoL.init(m_sampleRate, 8.0f, dsp::LFOWaveform::Sine);
    m_flutterLfoR.init(m_sampleRate, 9.5f, dsp::LFOWaveform::Sine);

    // Initialize hiss filters (shape noise to be more tape-like)
    // Tape hiss is mostly in the 2-8kHz range
    m_hissFilterL.initLowpass(m_sampleRate, 6000.0f);
    m_hissFilterR.initLowpass(m_sampleRate, 6000.0f);

    // Anti-aliasing filters for saturation (prevent harsh high frequencies)
    m_antiAliasL.initLowpass(m_sampleRate, 12000.0f);
    m_antiAliasR.initLowpass(m_sampleRate, 12000.0f);
}

float TapeEffect::saturate(float sample, float drive) {
    if (drive <= 0.0f) {
        return sample;
    }
    // Soft clipping using tanh
    // Scale drive from 0-1 to 1-4 for usable range
    float driveScaled = 1.0f + drive * 3.0f;
    return std::tanh(sample * driveScaled) / std::tanh(driveScaled);
}

float TapeEffect::generateHiss() {
    // Generate white noise and filter it
    float noise = m_noiseDist(m_rng);
    return noise;
}

void TapeEffect::processEffect(const float* input, float* output, uint32_t frames) {
    // Read parameters (with age modulation)
    float ageVal = static_cast<float>(age);
    float wowAmount = static_cast<float>(wow) * (1.0f + ageVal * 0.5f);
    float flutterAmount = static_cast<float>(flutter) * (1.0f + ageVal * 0.3f);
    float satAmount = static_cast<float>(saturation) + ageVal * 0.2f;
    float hissAmount = static_cast<float>(hiss) + ageVal * 0.1f;

    // Clamp combined values
    wowAmount = std::min(wowAmount, 1.0f);
    flutterAmount = std::min(flutterAmount, 1.0f);
    satAmount = std::min(satAmount, 1.0f);
    hissAmount = std::min(hissAmount, 1.0f);

    // Calculate delay parameters in samples
    float baseDelaySamples = BASE_DELAY_MS * static_cast<float>(m_sampleRate) / 1000.0f;
    float wowDepthSamples = MAX_WOW_DEPTH_MS * wowAmount * static_cast<float>(m_sampleRate) / 1000.0f;
    float flutterDepthSamples = MAX_FLUTTER_DEPTH_MS * flutterAmount * static_cast<float>(m_sampleRate) / 1000.0f;

    // Update LFO frequencies based on parameters
    // Wow: 0.5-2 Hz range
    float wowFreq = 0.5f + wowAmount * 1.5f;
    m_wowLfoL.setFrequency(wowFreq);
    m_wowLfoR.setFrequency(wowFreq * 0.95f);  // Slightly different for stereo

    // Flutter: 5-15 Hz range
    float flutterFreq = 5.0f + flutterAmount * 10.0f;
    m_flutterLfoL.setFrequency(flutterFreq);
    m_flutterLfoR.setFrequency(flutterFreq * 1.15f);  // Different for stereo

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // --- Apply saturation (before delay for authentic tape path) ---
        float satL = inL;
        float satR = inR;
        if (satAmount > 0.0f) {
            satL = saturate(inL, satAmount);
            satR = saturate(inR, satAmount);
            // Anti-alias filter to smooth harsh harmonics
            satL = m_antiAliasL.process(satL);
            satR = m_antiAliasR.process(satR);
        }

        // Write saturated signal to delay lines
        m_delayL.write(satL);
        m_delayR.write(satR);

        // --- Calculate wow modulation ---
        float wowL = m_wowLfoL.process();  // Returns -1 to 1
        float wowR = m_wowLfoR.process();

        // --- Calculate flutter modulation with randomized depth ---
        // Randomize flutter depth at each LFO cycle (Airwindows technique)
        float flutterPhaseL = m_flutterLfoL.phase();
        float flutterPhaseR = m_flutterLfoR.phase();

        // Detect phase wrap (new cycle)
        if (flutterPhaseL < m_prevFlutterPhaseL) {
            m_flutterDepthL = m_flutterDepthDist(m_rng);
        }
        if (flutterPhaseR < m_prevFlutterPhaseR) {
            m_flutterDepthR = m_flutterDepthDist(m_rng);
        }
        m_prevFlutterPhaseL = flutterPhaseL;
        m_prevFlutterPhaseR = flutterPhaseR;

        float flutterL = m_flutterLfoL.process() * m_flutterDepthL;
        float flutterR = m_flutterLfoR.process() * m_flutterDepthR;

        // --- Calculate total delay modulation ---
        float delayL = baseDelaySamples + wowL * wowDepthSamples + flutterL * flutterDepthSamples;
        float delayR = baseDelaySamples + wowR * wowDepthSamples + flutterR * flutterDepthSamples;

        // Clamp to valid range
        delayL = std::max(1.0f, delayL);
        delayR = std::max(1.0f, delayR);

        // Read from delay lines with interpolation
        float outL = m_delayL.readInterpolated(delayL);
        float outR = m_delayR.readInterpolated(delayR);

        // --- Add tape hiss ---
        if (hissAmount > 0.0f) {
            float hissL = generateHiss();
            float hissR = generateHiss();

            // Filter hiss to shape spectrum (tape hiss has specific character)
            hissL = m_hissFilterL.process(hissL);
            hissR = m_hissFilterR.process(hissR);

            // Scale hiss level (typically very quiet)
            float hissLevel = hissAmount * 0.03f;  // Max ~3% of signal
            outL += hissL * hissLevel;
            outR += hissR * hissLevel;
        }

        output[i * 2] = outL;
        output[i * 2 + 1] = outR;
    }
}

void TapeEffect::cleanupEffect() {
    m_delayL.clear();
    m_delayR.clear();
    m_wowLfoL.reset();
    m_wowLfoR.reset();
    m_flutterLfoL.reset();
    m_flutterLfoR.reset();
    m_hissFilterL.reset();
    m_hissFilterR.reset();
    m_antiAliasL.reset();
    m_antiAliasR.reset();
}

} // namespace vivid::audio
