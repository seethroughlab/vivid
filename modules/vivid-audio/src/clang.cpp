#include <vivid/audio/clang.h>
#include <vivid/operator_registry.h>
#include <vivid/audio_graph.h>
#include <vivid/context.h>
#include <vivid/viz_helpers.h>

#include <cmath>

namespace vivid::audio {

REGISTER_OPERATOR_EX(Clang, "Audio Drums", "Cowbell/clave/woodblock with square oscillators", false, vivid::OutputKind::Audio);

void Clang::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    m_filter.init(m_sampleRate);
    m_filter.setMode(dsp::SVFFilter::Mode::Bandpass);
    reset();
    m_initialized = true;
}

void Clang::process(Context& ctx) {
    if (!m_initialized) return;
}

void Clang::generateBlock(uint32_t frameCount) {
    if (!m_initialized) return;

    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    float freq = static_cast<float>(pitch);
    float toneAVal = static_cast<float>(toneA);
    float toneBVal = static_cast<float>(toneB);
    float ratioVal = static_cast<float>(ratio);
    float filterVal = static_cast<float>(filter);
    float noiseVal = static_cast<float>(noise);
    float decayTime = static_cast<float>(decay) * m_sampleRate;
    float vol = static_cast<float>(volume);

    float decayRate = (decayTime > 0) ? (1.0f / decayTime) : 1.0f;

    // Two frequencies at inharmonic ratio (classic cowbell)
    float freqA = freq;
    float freqB = freq * ratioVal;

    float phaseIncA = freqA / m_sampleRate;
    float phaseIncB = freqB / m_sampleRate;

    // Configure bandpass filter
    float filterFreq = freq * 1.2f + filterVal * freq * 0.8f;
    m_filter.setCutoff(filterFreq);
    m_filter.setResonance(0.3f + filterVal * 0.4f);

    for (uint32_t i = 0; i < frameCount; ++i) {
        // Generate two square waves
        float oscA = (m_phaseA < 0.5f) ? 1.0f : -1.0f;
        float oscB = (m_phaseB < 0.5f) ? 1.0f : -1.0f;

        // Mix oscillators
        float sample = oscA * toneAVal + oscB * toneBVal;

        // Add noise for body/texture
        if (noiseVal > 0.0f) {
            float noiseSample = generateNoise() * noiseVal * 0.5f;
            sample += noiseSample;
        }

        // Apply bandpass filter
        float filtered = m_filter.process(sample);
        sample = sample * (1.0f - filterVal) + filtered * filterVal;

        // Apply envelope
        sample *= m_env * vol;

        // Output stereo
        m_output.samples[i * 2] = sample;
        m_output.samples[i * 2 + 1] = sample;

        // Advance phases
        m_phaseA += phaseIncA;
        if (m_phaseA >= 1.0f) m_phaseA -= 1.0f;

        m_phaseB += phaseIncB;
        if (m_phaseB >= 1.0f) m_phaseB -= 1.0f;

        // Fast decay for percussive sound
        m_env *= (1.0f - decayRate * 0.5f);
    }
}

void Clang::handleEvent(const AudioEvent& event) {
    AudioOperator::handleEvent(event);

    if (event.type == AudioEventType::Reset) {
        reset();
    }
}

void Clang::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void Clang::onTrigger() {
    m_env = 1.0f;
    // Reset phases for consistent attack
    m_phaseA = 0.0f;
    m_phaseB = 0.0f;
}

void Clang::reset() {
    m_phaseA = 0.0f;
    m_phaseB = 0.0f;
    m_env = 0.0f;
    m_filter.reset();
}

float Clang::generateNoise() {
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
    return (static_cast<float>(m_seed) / 2147483648.0f) - 1.0f;
}

bool Clang::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    // Gold/bronze background for cowbell
    dl->AddRectFilled({minX, minY}, {maxX, maxY}, VIZ_COL32(50, 40, 25, 255), 4.0f);

    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;
    float maxSize = std::min(bounds.w, bounds.h) * 0.35f;

    // Draw cowbell shape as rectangle with envelope
    float size = maxSize * (0.5f + 0.5f * m_env);
    float w = size * 0.8f;
    float h = size * 0.6f;

    uint32_t bellColor = VIZ_COL32(
        200 + (int)(55 * m_env),
        180 + (int)(55 * m_env),
        100,
        (int)(255 * (0.3f + 0.7f * m_env))
    );

    // Draw as rectangle
    dl->AddRectFilled(
        {cx - w, cy - h * 0.5f},
        {cx + w, cy + h * 0.5f},
        bellColor, 4.0f
    );

    // Highlight line
    dl->AddLine(
        {cx - w * 0.8f, cy - h * 0.3f},
        {cx + w * 0.8f, cy - h * 0.3f},
        VIZ_COL32(255, 240, 180, (int)(150 * m_env)),
        2.0f
    );

    return true;
}

} // namespace vivid::audio
