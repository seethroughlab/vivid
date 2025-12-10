/**
 * @file audio_operator.cpp
 * @brief Implementation of AudioOperator base class
 */

#include <vivid/audio_operator.h>
#include <cstring>

namespace vivid {

const AudioBuffer* AudioOperator::inputBuffer(int index) const {
    AudioOperator* audioOp = audioInput(index);
    if (!audioOp) {
        return nullptr;
    }

    const AudioBuffer* buf = audioOp->outputBuffer();
    if (!buf || !buf->isValid()) {
        return nullptr;
    }

    return buf;
}

AudioOperator* AudioOperator::audioInput(int index) const {
    Operator* op = getInput(index);
    if (!op) {
        return nullptr;
    }

    // Check if it's an audio operator
    if (op->outputKind() != OutputKind::Audio) {
        return nullptr;
    }

    return static_cast<AudioOperator*>(op);
}

void AudioOperator::allocateOutput(uint32_t frames, uint32_t channels, uint32_t sampleRate) {
    m_output.allocate(frames, channels, sampleRate);
}

void AudioOperator::clearOutput() {
    m_output.clear();
}

void AudioOperator::releaseOutput() {
    m_output.release();
}

bool AudioOperator::copyInputToOutput(int index) {
    const AudioBuffer* in = inputBuffer(index);
    if (!in || !in->isValid()) {
        return false;
    }

    // Ensure output is allocated with same format
    if (m_output.frameCount != in->frameCount ||
        m_output.channels != in->channels ||
        m_output.sampleRate != in->sampleRate) {
        m_output.allocate(in->frameCount, in->channels, in->sampleRate);
    }

    // Copy samples
    std::memcpy(m_output.samples, in->samples, in->byteSize());
    return true;
}

} // namespace vivid
