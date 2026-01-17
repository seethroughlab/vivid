#include <vivid/audio/tom.h>
#include <vivid/operator_registry.h>
#include <vivid/audio_graph.h>
#include <vivid/context.h>
#include <vivid/viz_helpers.h>

#include <cmath>

namespace vivid::audio {

REGISTER_OPERATOR_EX(Tom, "Audio Drums", "Tom drum with resonant body filter", false, vivid::OutputKind::Audio);

void Tom::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    m_filter.init(m_sampleRate);
    m_filter.setMode(dsp::SVFFilter::Mode::Bandpass);
    reset();
    m_initialized = true;
}

void Tom::process(Context& ctx) {
    if (!m_initialized) return;
}

void Tom::generateBlock(uint32_t frameCount) {
    if (!m_initialized) return;

    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    float basePitch = static_cast<float>(pitch);
    float bendAmt = static_cast<float>(bend);
    float bendDecayTime = static_cast<float>(bendTime) * m_sampleRate;
    float colorAmt = static_cast<float>(color);
    float toneAmt = static_cast<float>(tone);
    float ampDecayTime = static_cast<float>(decay) * m_sampleRate;
    float vol = static_cast<float>(volume);

    float bendDecayRate = (bendDecayTime > 0) ? (1.0f / bendDecayTime) : 1.0f;
    float ampDecayRate = (ampDecayTime > 0) ? (1.0f / ampDecayTime) : 1.0f;

    // Configure filter - resonant bandpass at pitch frequency
    m_filter.setCutoff(basePitch * 1.5f);  // Slightly above fundamental
    m_filter.setResonance(0.3f + toneAmt * 0.5f);  // Resonance from tone param

    for (uint32_t i = 0; i < frameCount; ++i) {
        // Compute current frequency with pitch envelope
        // Bend amount is relative to base pitch (up to +100%)
        float freq = basePitch * (1.0f + bendAmt * m_pitchEnvValue);
        float phaseInc = freq / m_sampleRate;

        // Generate fundamental sine oscillator
        float osc = std::sin(m_phase * TWO_PI);

        // Add harmonics based on color
        if (colorAmt > 0.0f) {
            float harm2 = std::sin(m_phase2 * TWO_PI) * 0.4f;
            float harm3 = std::sin(m_phase3 * TWO_PI) * 0.2f;
            osc = osc * (1.0f - colorAmt * 0.4f) + (harm2 + harm3) * colorAmt;
        }

        // Apply resonant bandpass filter for tom body character
        float filtered = m_filter.process(osc);
        osc = osc * (1.0f - toneAmt) + filtered * toneAmt;

        // Apply amplitude envelope
        float sample = osc * m_ampEnv * vol;

        // Output stereo
        m_output.samples[i * 2] = sample;
        m_output.samples[i * 2 + 1] = sample;

        // Advance phases
        m_phase += phaseInc;
        if (m_phase >= 1.0f) m_phase -= 1.0f;

        m_phase2 += phaseInc * 2.0f;
        if (m_phase2 >= 1.0f) m_phase2 -= 1.0f;

        m_phase3 += phaseInc * 3.0f;
        if (m_phase3 >= 1.0f) m_phase3 -= 1.0f;

        // Decay envelopes
        m_pitchEnvValue *= (1.0f - bendDecayRate);
        m_ampEnv *= (1.0f - ampDecayRate * 0.08f);
        m_ampEnv = std::max(0.0f, m_ampEnv - ampDecayRate * 0.0005f);
    }
}

void Tom::handleEvent(const AudioEvent& event) {
    AudioOperator::handleEvent(event);

    if (event.type == AudioEventType::Reset) {
        reset();
    }
}

void Tom::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void Tom::midiNoteOn(uint8_t /*note*/, float velocity, uint8_t /*channel*/) {
    m_velocity = velocity;
    m_ampEnv = velocity;
    m_pitchEnvValue = 1.0f;
}

void Tom::midiNoteOff(uint8_t /*note*/, float /*velocity*/, uint8_t /*channel*/) {
    // One-shot drum, nothing to do
}

void Tom::onTrigger() {
    midiNoteOn(0, 1.0f, 0);
}

void Tom::reset() {
    m_phase = 0.0f;
    m_phase2 = 0.0f;
    m_phase3 = 0.0f;
    m_ampEnv = 0.0f;
    m_pitchEnvValue = 0.0f;
    m_filter.reset();
}

bool Tom::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    viz.drawBackground(bounds, VizColors::BackgroundDark);

    // Amplitude envelope bar
    VizBounds barBounds = bounds.inset(bounds.w * 0.2f, 4.0f);
    viz.drawEnvelopeBar(barBounds, m_ampEnv, VizColors::EnvelopeWarm);

    // Pitch indicator line
    if (m_pitchEnvValue > 0.01f) {
        float pitchY = bounds.y + 8 + (bounds.h - 16) * (1.0f - m_pitchEnvValue);
        dl->AddLine({barBounds.x - 4, pitchY}, {barBounds.right() + 4, pitchY},
                    VizColors::Active, 2.0f);
    }

    return true;
}

} // namespace vivid::audio
