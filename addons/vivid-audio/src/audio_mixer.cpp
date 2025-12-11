#include <vivid/audio/audio_mixer.h>
#include <vivid/context.h>
#include <algorithm>

namespace vivid::audio {

AudioMixer& AudioMixer::input(int index, const std::string& name) {
    if (index >= 0 && index < MAX_INPUTS) {
        m_inputNames[index] = name;
    }
    return *this;
}

AudioMixer& AudioMixer::gain(int index, float g) {
    if (index >= 0 && index < MAX_INPUTS) {
        m_gains[index] = g;
    }
    return *this;
}

void AudioMixer::init(Context& ctx) {
    allocateOutput();

    // Resolve input operators by name and register dependencies
    auto& chain = ctx.chain();
    for (int i = 0; i < MAX_INPUTS; ++i) {
        if (!m_inputNames[i].empty()) {
            Operator* op = chain.getByName(m_inputNames[i]);
            m_inputs[i] = dynamic_cast<AudioOperator*>(op);
            if (m_inputs[i]) {
                // Register as input for topological sort
                setInput(i, m_inputs[i]);
            }
            // Set default gain if not explicitly set
            if (m_gains[i] == 0.0f) {
                m_gains[i] = 1.0f;
            }
        } else {
            m_inputs[i] = nullptr;
        }
    }

    m_initialized = true;
}

void AudioMixer::process(Context& ctx) {
    if (!m_initialized) return;

    uint32_t frames = m_output.frameCount;
    float vol = static_cast<float>(m_volume);

    // Clear output buffer
    for (uint32_t i = 0; i < frames * 2; ++i) {
        m_output.samples[i] = 0.0f;
    }

    // Sum all inputs
    for (int ch = 0; ch < MAX_INPUTS; ++ch) {
        if (m_inputs[ch]) {
            const AudioBuffer* inBuf = m_inputs[ch]->outputBuffer();
            if (inBuf && inBuf->isValid()) {
                float g = m_gains[ch];
                uint32_t inFrames = std::min(frames, inBuf->frameCount);
                for (uint32_t i = 0; i < inFrames * 2; ++i) {
                    m_output.samples[i] += inBuf->samples[i] * g;
                }
            }
        }
    }

    // Apply master volume
    for (uint32_t i = 0; i < frames * 2; ++i) {
        m_output.samples[i] *= vol;
    }
}

void AudioMixer::cleanup() {
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::audio
