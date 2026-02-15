#include <vivid/audio/cymbal.h>
#include <vivid/operator_registry.h>
#include <vivid/audio_graph.h>
#include <vivid/context.h>
#include <vivid/viz_helpers.h>

#include <cmath>

namespace vivid::audio {

REGISTER_OPERATOR_EX(Cymbal, "Audio Drums", "Crash/ride cymbal with shimmer", false, vivid::OutputKind::Audio);

void Cymbal::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    m_filter.init(m_sampleRate);
    m_filter.setMode(dsp::SVFFilter::Mode::Highpass);
    reset();
    m_initialized = true;
}

void Cymbal::process(Context& ctx) {
    if (!m_initialized) return;
}

void Cymbal::generateBlock(uint32_t frameCount) {
    if (!m_initialized) return;

    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    float decayTime = static_cast<float>(decay) * m_sampleRate;
    float toneAmt = static_cast<float>(tone);
    float pitchMult = static_cast<float>(pitch);
    float shimmerAmt = static_cast<float>(shimmer);
    float sizzleAmt = static_cast<float>(sizzle);
    float vol = static_cast<float>(volume);

    float decayRate = (decayTime > 0) ? (1.0f / decayTime) : 1.0f;

    // 12 ring oscillator frequencies - more complex than hihat
    // Based on cymbal physics - inharmonic frequencies
    const float baseRingFreqs[NUM_RINGS] = {
        205.3f, 304.4f, 369.6f, 411.5f,
        522.7f, 540.0f, 587.2f, 698.5f,
        800.0f, 880.0f, 1046.5f, 1174.7f
    };

    // Configure filter for brightness
    float cutoffHz = 3000.0f + toneAmt * 9000.0f;  // 3-12kHz
    m_filter.setCutoff(cutoffHz);
    m_filter.setResonance(0.2f);

    // Shimmer LFO frequency (slow wobble)
    float shimmerFreq = 4.0f + shimmerAmt * 8.0f;  // 4-12Hz

    for (uint32_t i = 0; i < frameCount; ++i) {
        // Sum of 12 ring oscillators (square waves)
        float ringSum = 0.0f;
        for (int r = 0; r < NUM_RINGS; ++r) {
            float freq = baseRingFreqs[r] * pitchMult;
            float phaseInc = freq / m_sampleRate;
            ringSum += (m_ringPhase[r] < 0.5f) ? 1.0f : -1.0f;
            m_ringPhase[r] += phaseInc;
            if (m_ringPhase[r] >= 1.0f) m_ringPhase[r] -= 1.0f;
        }
        ringSum /= NUM_RINGS;

        // Add noise for sizzle
        float noise = generateNoise() * sizzleAmt;

        // Mix ring and noise
        float sample = ringSum * (1.0f - sizzleAmt * 0.3f) + noise;

        // Apply highpass filter
        sample = m_filter.process(sample);

        // Apply shimmer modulation
        if (shimmerAmt > 0.0f) {
            float shimmerLFO = 0.5f + 0.5f * std::sin(m_shimmerPhase * TWO_PI);
            float modulation = 1.0f - shimmerAmt * 0.5f * (1.0f - shimmerLFO);
            sample *= modulation;
            m_shimmerPhase += shimmerFreq / m_sampleRate;
            if (m_shimmerPhase >= 1.0f) m_shimmerPhase -= 1.0f;
        }

        // Apply envelope
        sample *= m_env * vol;

        // Output stereo
        m_output.samples[i * 2] = sample;
        m_output.samples[i * 2 + 1] = sample;

        // Very slow exponential decay for long sustain
        m_env *= (1.0f - decayRate * 0.05f);
        m_env = std::max(0.0f, m_env - decayRate * 0.00001f);
    }
}

void Cymbal::handleEvent(const AudioEvent& event) {
    if (event.type == AudioEventType::Trigger) {
        float velocity = (event.value1 > 0.0f) ? event.value1 : 1.0f;
        midiNoteOn(0, velocity, 0);
    } else if (event.type == AudioEventType::NoteOff) {
        choke();
    } else if (event.type == AudioEventType::Reset) {
        reset();
    }
}

void Cymbal::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void Cymbal::midiNoteOn(uint8_t /*note*/, float velocity, uint8_t /*channel*/) {
    m_velocity = velocity;
    m_env = velocity;
    m_shimmerPhase = 0.0f;
}

void Cymbal::midiNoteOff(uint8_t /*note*/, float /*velocity*/, uint8_t /*channel*/) {
    // Note-off chokes the cymbal
    choke();
}

void Cymbal::choke() {
    m_env = 0.0f;
}

void Cymbal::reset() {
    m_env = 0.0f;
    for (int i = 0; i < NUM_RINGS; ++i) m_ringPhase[i] = 0.0f;
    m_shimmerPhase = 0.0f;
    m_filter.reset();
}

float Cymbal::generateNoise() {
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
    return (static_cast<float>(m_seed) / 2147483648.0f) - 1.0f;
}

bool Cymbal::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;

    // Dark gold background
    dl->AddRectFilled({minX, minY}, {maxX, maxY}, VIZ_COL32(40, 35, 25, 255), 4.0f);

    float env = m_env;
    float maxRadius = std::min(bounds.w, bounds.h) * 0.4f;

    // Draw concentric rings for cymbal
    int numRings = 5;
    for (int r = numRings - 1; r >= 0; --r) {
        float radius = maxRadius * ((r + 1) / (float)numRings) * (0.5f + 0.5f * env);
        float alpha = env * (0.3f + 0.7f * (r / (float)numRings));
        uint32_t ringColor = VIZ_COL32(
            200 + (int)(55 * env),
            180 + (int)(55 * env),
            100,
            (int)(200 * alpha)
        );
        dl->AddCircle({cx, cy}, radius, ringColor, 24, 1.5f);
    }

    // Center highlight
    dl->AddCircleFilled({cx, cy}, 4.0f + env * 6.0f,
        VIZ_COL32(255, 240, 180, (int)(255 * env)));

    return true;
}

} // namespace vivid::audio
