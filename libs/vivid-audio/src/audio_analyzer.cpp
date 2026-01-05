#include <vivid/audio/audio_analyzer.h>
#include <vivid/chain.h>
#include <vivid/context.h>
#include <iostream>

namespace vivid::audio {

void AudioAnalyzer::init(Context& ctx) {
    // Find and connect to input operator
    if (!m_inputName.empty()) {
        Operator* op = ctx.chain().getByName(m_inputName);
        if (op && op->outputKind() == OutputKind::Audio) {
            m_connectedInput = static_cast<AudioOperator*>(op);
            setInput(0, op);  // Set base class input for dependency tracking
        } else if (op) {
            std::cerr << "[" << name() << "] Input '" << m_inputName
                      << "' is not an audio operator" << std::endl;
        } else {
            std::cerr << "[" << name() << "] Input '" << m_inputName
                      << "' not found" << std::endl;
        }
    }

    // Initialize analyzer-specific state
    initAnalyzer(ctx);
}

void AudioAnalyzer::process(Context& ctx) {
    // Check if input needs reconnecting (name may have changed at runtime)
    if (!m_inputName.empty()) {
        Operator* op = ctx.chain().getByName(m_inputName);
        if (op && op->outputKind() == OutputKind::Audio) {
            AudioOperator* audioOp = static_cast<AudioOperator*>(op);
            if (audioOp != m_connectedInput) {
                m_connectedInput = audioOp;
                setInput(0, op);
            }
        }
    }

    // Get input buffer
    const AudioBuffer* inputBuf = getInputBuffer();
    if (!inputBuf || !inputBuf->isValid()) {
        return;  // No input to analyze
    }

    // Run analysis
    analyze(inputBuf->samples, inputBuf->frameCount, inputBuf->channels);
}

void AudioAnalyzer::cleanup() {
    cleanupAnalyzer();
    m_connectedInput = nullptr;
}

const AudioBuffer* AudioAnalyzer::getInputBuffer() const {
    if (m_connectedInput) {
        return m_connectedInput->outputBuffer();
    }
    return nullptr;
}

} // namespace vivid::audio
