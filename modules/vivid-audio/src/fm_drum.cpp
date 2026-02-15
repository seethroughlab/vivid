#include <vivid/audio/fm_drum.h>
#include <vivid/operator_registry.h>
#include <vivid/audio_graph.h>
#include <vivid/context.h>
#include <vivid/viz_helpers.h>

#include <cmath>

namespace vivid::audio {

REGISTER_OPERATOR_EX(FMDrum, "Audio Drums", "2-operator FM drum for metallic sounds", false, vivid::OutputKind::Audio);

void FMDrum::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    m_filter.init(m_sampleRate);
    m_filter.setMode(dsp::SVFFilter::Mode::Lowpass);
    reset();
    m_initialized = true;
}

void FMDrum::process(Context& ctx) {
    if (!m_initialized) return;
}

void FMDrum::generateBlock(uint32_t frameCount) {
    if (!m_initialized) return;

    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    float freq = static_cast<float>(pitch);
    float ratioVal = static_cast<float>(ratio);
    float amountVal = static_cast<float>(amount);
    float fbVal = static_cast<float>(feedback);
    float toneVal = static_cast<float>(tone);
    float decayTime = static_cast<float>(decay) * m_sampleRate;
    float modDecayTime = static_cast<float>(modDecay) * m_sampleRate;
    float vol = static_cast<float>(volume);

    float ampDecayRate = (decayTime > 0) ? (1.0f / decayTime) : 1.0f;
    float modDecayRate = (modDecayTime > 0) ? (1.0f / modDecayTime) : 1.0f;

    // Modulator frequency
    float modFreq = freq * ratioVal;

    // Phase increments
    float carrierPhaseInc = freq / m_sampleRate;
    float modPhaseInc = modFreq / m_sampleRate;

    // Configure lowpass filter based on tone
    float cutoff = 2000.0f + (1.0f - toneVal) * 18000.0f;  // 2-20kHz
    m_filter.setCutoff(cutoff);
    m_filter.setResonance(0.1f);

    for (uint32_t i = 0; i < frameCount; ++i) {
        // Modulator with self-feedback
        float fbSample = m_feedbackSample * fbVal * 0.5f;
        float modulator = std::sin((m_modPhase + fbSample) * TWO_PI);
        m_feedbackSample = modulator;

        // Apply modulation envelope to FM amount
        float fmAmount = amountVal * m_modEnv * 5.0f;  // Scale for audible modulation

        // Carrier with FM from modulator
        float carrier = std::sin((m_carrierPhase + modulator * fmAmount) * TWO_PI);

        // Apply lowpass filter
        float sample = m_filter.process(carrier);

        // Apply amplitude envelope
        sample *= m_ampEnv * vol;

        // Output stereo
        m_output.samples[i * 2] = sample;
        m_output.samples[i * 2 + 1] = sample;

        // Advance phases
        m_carrierPhase += carrierPhaseInc;
        if (m_carrierPhase >= 1.0f) m_carrierPhase -= 1.0f;

        m_modPhase += modPhaseInc;
        if (m_modPhase >= 1.0f) m_modPhase -= 1.0f;

        // Decay envelopes
        m_ampEnv *= (1.0f - ampDecayRate * 0.1f);
        m_ampEnv = std::max(0.0f, m_ampEnv - ampDecayRate * 0.0005f);

        m_modEnv *= (1.0f - modDecayRate);
    }
}

void FMDrum::handleEvent(const AudioEvent& event) {
    if (event.type == AudioEventType::Trigger) {
        float velocity = (event.value1 > 0.0f) ? event.value1 : 1.0f;
        midiNoteOn(0, velocity, 0);
    } else if (event.type == AudioEventType::Reset) {
        reset();
    }
}

void FMDrum::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void FMDrum::midiNoteOn(uint8_t /*note*/, float velocity, uint8_t /*channel*/) {
    m_velocity = velocity;
    m_ampEnv = velocity;
    m_modEnv = velocity;
    // Don't reset phases for natural retriggering
}

void FMDrum::midiNoteOff(uint8_t /*note*/, float /*velocity*/, uint8_t /*channel*/) {
    // One-shot drum, nothing to do
}

void FMDrum::reset() {
    m_carrierPhase = 0.0f;
    m_modPhase = 0.0f;
    m_feedbackSample = 0.0f;
    m_ampEnv = 0.0f;
    m_modEnv = 0.0f;
    m_filter.reset();
}

bool FMDrum::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    // Dark blue-purple background
    dl->AddRectFilled({minX, minY}, {maxX, maxY}, VIZ_COL32(35, 30, 55, 255), 4.0f);

    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;

    // Draw FM modulation visualization
    float maxRadius = std::min(bounds.w, bounds.h) * 0.35f;

    // Carrier circle
    float carrierRadius = maxRadius * m_ampEnv;
    dl->AddCircle({cx, cy}, carrierRadius,
        VIZ_COL32(100, 150, 255, (int)(200 * m_ampEnv)), 24, 2.0f);

    // Modulator orbit
    float modRadius = maxRadius * 0.5f * m_modEnv;
    float modAngle = m_modPhase * 6.28318f;
    float modX = cx + std::cos(modAngle) * carrierRadius * 0.7f;
    float modY = cy + std::sin(modAngle) * carrierRadius * 0.7f;
    dl->AddCircleFilled({modX, modY}, modRadius * 0.3f + 3.0f,
        VIZ_COL32(255, 150, 100, (int)(255 * m_modEnv)));

    // Center dot
    dl->AddCircleFilled({cx, cy}, 4.0f + m_ampEnv * 4.0f,
        VIZ_COL32(200, 200, 255, (int)(255 * m_ampEnv)));

    return true;
}

} // namespace vivid::audio
