#include <vivid/audio/drum_stack.h>
#include <vivid/operator_registry.h>
#include <vivid/audio_graph.h>
#include <vivid/context.h>
#include <vivid/viz_helpers.h>

#include <cmath>

namespace vivid::audio {

REGISTER_OPERATOR_EX(DrumStack, "Audio Drums", "Layer multiple drums together", false, vivid::OutputKind::Audio);

void DrumStack::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    m_inputsResolved = false;
    m_initialized = true;
}

void DrumStack::process(Context& ctx) {
    if (!m_initialized) return;

    // Resolve input operators if not done yet
    if (!m_inputsResolved) {
        resolveInputs();
    }
}

void DrumStack::resolveInputs() {
    // This will be called during process to resolve operator names to pointers
    // For now, we rely on the audio graph to do this during generateBlock
    m_inputsResolved = true;
}

void DrumStack::generateBlock(uint32_t frameCount) {
    if (!m_initialized) return;

    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    float mix1Val = static_cast<float>(mix1);
    float mix2Val = static_cast<float>(mix2);
    float mix3Val = static_cast<float>(mix3);
    float vol = static_cast<float>(volume);

    // Get audio from each layer
    const AudioBuffer* buf1 = nullptr;
    const AudioBuffer* buf2 = nullptr;
    const AudioBuffer* buf3 = nullptr;

    if (m_layer1) buf1 = m_layer1->outputBuffer();
    if (m_layer2) buf2 = m_layer2->outputBuffer();
    if (m_layer3) buf3 = m_layer3->outputBuffer();

    for (uint32_t i = 0; i < frameCount; ++i) {
        float sampleL = 0.0f;
        float sampleR = 0.0f;

        // Mix layer 1
        if (buf1 && buf1->samples && i < buf1->frameCount) {
            sampleL += buf1->samples[i * 2] * mix1Val;
            sampleR += buf1->samples[i * 2 + 1] * mix1Val;
        }

        // Mix layer 2
        if (buf2 && buf2->samples && i < buf2->frameCount) {
            sampleL += buf2->samples[i * 2] * mix2Val;
            sampleR += buf2->samples[i * 2 + 1] * mix2Val;
        }

        // Mix layer 3
        if (buf3 && buf3->samples && i < buf3->frameCount) {
            sampleL += buf3->samples[i * 2] * mix3Val;
            sampleR += buf3->samples[i * 2 + 1] * mix3Val;
        }

        // Apply master volume
        m_output.samples[i * 2] = sampleL * vol;
        m_output.samples[i * 2 + 1] = sampleR * vol;
    }
}

void DrumStack::handleEvent(const AudioEvent& event) {
    // Forward triggers to all layers
    if (event.type == AudioEventType::Trigger) {
        if (m_layer1) m_layer1->trigger();
        if (m_layer2) m_layer2->trigger();
        if (m_layer3) m_layer3->trigger();
    }

    if (event.type == AudioEventType::Reset) {
        reset();
    }
}

void DrumStack::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void DrumStack::midiNoteOn(uint8_t note, float velocity, uint8_t channel) {
    // Route MIDI to all layers (they're mixed, not note-mapped)
    if (auto* r1 = dynamic_cast<MidiReceiver*>(m_layer1)) r1->midiNoteOn(note, velocity, channel);
    if (auto* r2 = dynamic_cast<MidiReceiver*>(m_layer2)) r2->midiNoteOn(note, velocity, channel);
    if (auto* r3 = dynamic_cast<MidiReceiver*>(m_layer3)) r3->midiNoteOn(note, velocity, channel);
}

void DrumStack::midiNoteOff(uint8_t note, float velocity, uint8_t channel) {
    // Route note-off to all layers
    if (auto* r1 = dynamic_cast<MidiReceiver*>(m_layer1)) r1->midiNoteOff(note, velocity, channel);
    if (auto* r2 = dynamic_cast<MidiReceiver*>(m_layer2)) r2->midiNoteOff(note, velocity, channel);
    if (auto* r3 = dynamic_cast<MidiReceiver*>(m_layer3)) r3->midiNoteOff(note, velocity, channel);
}

void DrumStack::reset() {
    // Nothing to reset in the stack itself
}

bool DrumStack::isActive() const {
    // Active if any layer is active
    // For now, just return if we have any layers set up
    return (m_layer1 != nullptr || m_layer2 != nullptr || m_layer3 != nullptr);
}

bool DrumStack::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    // Dark background
    dl->AddRectFilled({minX, minY}, {maxX, maxY}, VIZ_COL32(35, 35, 40, 255), 4.0f);

    // Draw 3 stacked bars representing layers
    float barH = bounds.h * 0.25f;
    float gap = 4.0f;
    float barW = bounds.w - 16.0f;
    float x = minX + 8.0f;

    float mix1Val = static_cast<float>(mix1);
    float mix2Val = static_cast<float>(mix2);
    float mix3Val = static_cast<float>(mix3);

    // Layer 1 (bottom)
    float y1 = maxY - 8.0f - barH;
    float w1 = barW * mix1Val;
    dl->AddRectFilled({x, y1}, {x + w1, y1 + barH},
        VIZ_COL32(100, 200, 150, 200), 2.0f);
    dl->AddRect({x, y1}, {x + barW, y1 + barH},
        VIZ_COL32(100, 200, 150, 100), 2.0f, 0, 1.0f);

    // Layer 2 (middle)
    float y2 = y1 - barH - gap;
    float w2 = barW * mix2Val;
    dl->AddRectFilled({x, y2}, {x + w2, y2 + barH},
        VIZ_COL32(200, 150, 100, 200), 2.0f);
    dl->AddRect({x, y2}, {x + barW, y2 + barH},
        VIZ_COL32(200, 150, 100, 100), 2.0f, 0, 1.0f);

    // Layer 3 (top)
    float y3 = y2 - barH - gap;
    float w3 = barW * mix3Val;
    dl->AddRectFilled({x, y3}, {x + w3, y3 + barH},
        VIZ_COL32(150, 100, 200, 200), 2.0f);
    dl->AddRect({x, y3}, {x + barW, y3 + barH},
        VIZ_COL32(150, 100, 200, 100), 2.0f, 0, 1.0f);

    return true;
}

} // namespace vivid::audio
