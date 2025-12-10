#include <vivid/audio/audio_effect.h>
#include <vivid/chain.h>
#include <vivid/context.h>
#include <algorithm>
#include <iostream>

namespace vivid::audio {

void AudioEffect::init(Context& ctx) {
    // Allocate output buffer
    allocateOutput();

    // Find and connect to input operator
    if (!m_inputName.empty()) {
        Operator* op = ctx.chain().getByName(m_inputName);
        if (op && op->outputKind() == OutputKind::Audio) {
            m_connectedInput = static_cast<AudioOperator*>(op);
        } else if (op) {
            std::cerr << "[" << name() << "] Input '" << m_inputName
                      << "' is not an audio operator" << std::endl;
        } else {
            std::cerr << "[" << name() << "] Input '" << m_inputName
                      << "' not found" << std::endl;
        }
    }

    // Initialize effect-specific state
    initEffect(ctx);
}

void AudioEffect::process(Context& ctx) {
    // Get input buffer
    const AudioBuffer* inputBuf = nullptr;
    if (m_connectedInput) {
        inputBuf = m_connectedInput->outputBuffer();
    }

    // No input: output silence based on requested frame count
    if (!inputBuf || !inputBuf->isValid()) {
        uint32_t frames = ctx.audioFramesThisFrame();
        if (m_output.frameCount != frames) {
            m_output.allocate(frames, AUDIO_CHANNELS, AUDIO_SAMPLE_RATE);
        }
        clearOutput();
        return;
    }

    uint32_t frames = inputBuf->frameCount;
    uint32_t channels = inputBuf->channels;

    // Ensure output buffer is sized correctly (matches input)
    if (m_output.frameCount != frames || m_output.channels != channels) {
        m_output.allocate(frames, channels, inputBuf->sampleRate);
    }

    // Bypass: copy input to output directly
    if (m_bypass) {
        std::copy(inputBuf->samples,
                  inputBuf->samples + inputBuf->sampleCount(),
                  m_output.samples);
        return;
    }

    // Process the effect
    if (m_mix >= 1.0f) {
        // Fully wet: process directly to output
        processEffect(inputBuf->samples, m_output.samples, frames);
    } else if (m_mix <= 0.0f) {
        // Fully dry: copy input to output
        std::copy(inputBuf->samples,
                  inputBuf->samples + inputBuf->sampleCount(),
                  m_output.samples);
    } else {
        // Mix: process to output, then blend with input
        processEffect(inputBuf->samples, m_output.samples, frames);

        // Blend: output = dry * (1 - mix) + wet * mix
        float dry = 1.0f - m_mix;
        float wet = m_mix;
        uint32_t totalSamples = frames * channels;
        for (uint32_t i = 0; i < totalSamples; ++i) {
            m_output.samples[i] = inputBuf->samples[i] * dry +
                                   m_output.samples[i] * wet;
        }
    }
}

void AudioEffect::cleanup() {
    cleanupEffect();
    releaseOutput();
    m_connectedInput = nullptr;
}

} // namespace vivid::audio
