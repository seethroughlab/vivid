// Vivid - Chain Implementation

#include <vivid/chain.h>
#include <vivid/context.h>
#include <vivid/operator.h>
#include <vivid/effects/texture_operator.h>
#include <vivid/audio_operator.h>
#include <vivid/audio_output.h>
#include <vivid/audio_buffer.h>
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <iostream>
#include <functional>
#include <cstring>
#include <cstdlib>

namespace vivid {

// Check environment variable for debug mode
void Chain::checkDebugEnvVar() {
    if (m_debugEnvChecked) return;
    m_debugEnvChecked = true;

    const char* envVal = std::getenv("VIVID_DEBUG_CHAIN");
    if (envVal && (std::string(envVal) == "1" || std::string(envVal) == "true")) {
        m_debug = true;
        std::cout << "[Chain Debug] Debug mode enabled via VIVID_DEBUG_CHAIN" << std::endl;
    }
}

void Chain::debugOutputPath(const std::string& startName) {
    std::string name = startName.empty() ? m_outputName : startName;
    if (name.empty()) {
        std::cout << "[Chain Debug] No output operator set" << std::endl;
        return;
    }

    std::cout << "[Chain Debug] Output path: ";
    Operator* current = getByName(name);
    bool first = true;

    while (current) {
        std::string opName = getName(current);
        if (!first) std::cout << " -> ";
        std::cout << opName;
        first = false;

        // Find who uses this operator as input
        Operator* nextInChain = nullptr;
        for (const auto& [n, op] : m_operators) {
            for (size_t i = 0; i < op->inputCount(); ++i) {
                if (op->getInput(i) == current) {
                    // This op uses current as input, so current feeds into this
                    // But we want to trace FORWARD to output, not backward
                    break;
                }
            }
        }

        // Actually we want to trace from current to the designated output
        // This is harder - let's just show what's connected
        if (current->inputCount() > 0) {
            current = current->getInput(0);
        } else {
            current = nullptr;
        }
    }

    if (name == m_outputName) {
        std::cout << " -> SCREEN";
    }
    std::cout << std::endl;
}

Operator* Chain::addOperator(const std::string& name, Operator* op) {
    // Take ownership of the operator using unique_ptr
    // This function lives in the exe, so the unique_ptr uses the exe's allocator
    m_operators[name] = std::unique_ptr<Operator>(op);
    m_orderedNames.push_back(name);
    m_operatorNames[op] = name;
    m_needsSort = true;
    return op;
}

Operator* Chain::getByName(const std::string& name) {
    auto it = m_operators.find(name);
    if (it == m_operators.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::string Chain::getName(Operator* op) const {
    auto it = m_operatorNames.find(op);
    if (it == m_operatorNames.end()) {
        return "";
    }
    return it->second;
}

Operator* Chain::getOutput() const {
    if (m_outputName.empty()) {
        return nullptr;
    }
    auto it = m_operators.find(m_outputName);
    return it != m_operators.end() ? it->second.get() : nullptr;
}

void Chain::audioOutput(const std::string& name) {
    Operator* op = getByName(name);
    if (!op) {
        m_error = "Audio output operator '" + name + "' not found";
        return;
    }
    if (op->outputKind() != OutputKind::Audio) {
        m_error = "Audio output operator must produce audio. '" + name + "' produces " +
                 outputKindName(op->outputKind()) + ".";
        return;
    }
    m_audioOutputName = name;
}

Operator* Chain::getAudioOutput() const {
    if (m_audioOutputName.empty()) {
        return nullptr;
    }
    auto it = m_operators.find(m_audioOutputName);
    return it != m_operators.end() ? it->second.get() : nullptr;
}

const AudioBuffer* Chain::audioOutputBuffer() const {
    Operator* op = getAudioOutput();
    if (!op || op->outputKind() != OutputKind::Audio) {
        return nullptr;
    }
    // Cast to AudioOperator and get buffer
    AudioOperator* audioOp = static_cast<AudioOperator*>(op);
    return audioOp->outputBuffer();
}

void Chain::generateAudioForExport(float* output, uint32_t frameCount) {
    if (!m_audioOutput) {
        std::memset(output, 0, frameCount * AUDIO_CHANNELS * sizeof(float));
        return;
    }
    m_audioOutput->generateForExport(output, frameCount);
}

void Chain::startAudioRecordingTap() {
    if (m_audioOutput) {
        m_audioOutput->startRecordingTap();
    }
}

void Chain::stopAudioRecordingTap() {
    if (m_audioOutput) {
        m_audioOutput->stopRecordingTap();
    }
}

uint32_t Chain::popAudioRecordedSamples(float* output, uint32_t maxFrames) {
    if (!m_audioOutput) {
        return 0;
    }
    return m_audioOutput->popRecordedSamples(output, maxFrames);
}

void Chain::buildDependencyGraph() {
    // For each operator, find which other operators it depends on
    // by looking at its inputs
    for (const auto& [name, op] : m_operators) {
        for (size_t i = 0; i < op->inputCount(); ++i) {
            Operator* input = op->getInput(i);
            if (input) {
                // This operator depends on the input operator
                // We need this for topological sort
            }
        }
    }
}

bool Chain::detectCycle() {
    // Use DFS with three-color marking to detect cycles
    enum class Color { White, Gray, Black };
    std::unordered_map<Operator*, Color> colors;

    for (const auto& [name, op] : m_operators) {
        colors[op.get()] = Color::White;
    }

    std::function<bool(Operator*)> hasCycle = [&](Operator* node) -> bool {
        colors[node] = Color::Gray;

        for (size_t i = 0; i < node->inputCount(); ++i) {
            Operator* input = node->getInput(i);
            if (input && colors.count(input)) {
                if (colors[input] == Color::Gray) {
                    // Found a back edge - cycle detected
                    return true;
                }
                if (colors[input] == Color::White && hasCycle(input)) {
                    return true;
                }
            }
        }

        colors[node] = Color::Black;
        return false;
    };

    for (const auto& [name, op] : m_operators) {
        if (colors[op.get()] == Color::White) {
            if (hasCycle(op.get())) {
                return true;
            }
        }
    }

    return false;
}

void Chain::computeExecutionOrder() {
    if (!m_needsSort) {
        return;
    }

    m_executionOrder.clear();
    m_error.clear();

    // Check for cycles first
    if (detectCycle()) {
        m_error = "Circular dependency detected in operator chain";
        return;
    }

    // Kahn's algorithm for topological sort
    // Count incoming edges (dependencies) for each operator
    std::unordered_map<Operator*, int> inDegree;
    std::unordered_map<Operator*, std::vector<Operator*>> dependents;

    for (const auto& [name, op] : m_operators) {
        inDegree[op.get()] = 0;
    }

    // Build reverse dependency graph and count in-degrees
    for (const auto& [name, op] : m_operators) {
        for (size_t i = 0; i < op->inputCount(); ++i) {
            Operator* input = op->getInput(i);
            if (input && m_operators.count(getName(input))) {
                inDegree[op.get()]++;
                dependents[input].push_back(op.get());
            }
        }
    }

    // Start with operators that have no dependencies
    std::queue<Operator*> ready;
    for (const auto& [op, degree] : inDegree) {
        if (degree == 0) {
            ready.push(op);
        }
    }

    // Process operators in dependency order
    while (!ready.empty()) {
        Operator* current = ready.front();
        ready.pop();
        m_executionOrder.push_back(current);

        // Reduce in-degree for all dependents
        for (Operator* dependent : dependents[current]) {
            inDegree[dependent]--;
            if (inDegree[dependent] == 0) {
                ready.push(dependent);
            }
        }
    }

    // Check if all operators were processed
    if (m_executionOrder.size() != m_operators.size()) {
        m_error = "Could not resolve operator dependencies (possible cycle)";
        return;
    }

    m_needsSort = false;
}

void Chain::init(Context& ctx) {
    // Check for debug environment variable
    checkDebugEnvVar();

    // First pass: resolve string-based inputs for texture operators
    // This must happen before computeExecutionOrder() so the topological
    // sort can see the actual dependencies
    for (const auto& [name, op] : m_operators) {
        if (auto* texOp = dynamic_cast<effects::TextureOperator*>(op.get())) {
            texOp->resolveInputs(*this);
        }
    }

    // Second pass: call init on all operators
    for (const auto& [name, op] : m_operators) {
        op->init(ctx);
    }

    // Now compute execution order with resolved dependencies
    computeExecutionOrder();

    if (hasError()) {
        ctx.setError(m_error);
        return;
    }

    // Validate texture output
    if (m_outputName.empty()) {
        std::cerr << "[Chain Warning] No output specified. Screen will be black. "
                  << "Use chain.output(\"name\") to designate output." << std::endl;
    } else {
        Operator* out = getByName(m_outputName);
        if (!out) {
            m_error = "Output operator '" + m_outputName + "' not found";
            ctx.setError(m_error);
            return;
        }
        if (out->outputKind() != OutputKind::Texture) {
            m_error = "Output operator '" + m_outputName + "' produces " +
                     outputKindName(out->outputKind()) + ", not Texture. Route through Render3D first.";
            ctx.setError(m_error);
            return;
        }
    }

    // Validate audio output (if specified)
    if (!m_audioOutputName.empty()) {
        Operator* audioOut = getByName(m_audioOutputName);
        if (!audioOut) {
            m_error = "Audio output operator '" + m_audioOutputName + "' not found";
            ctx.setError(m_error);
            return;
        }
        // OutputKind::Audio check already exists in audioOutput()
    }

    // Separate audio and visual operators
    // Audio operators go to AudioGraph (processed on audio thread)
    // Visual operators stay in m_visualExecutionOrder (processed on main thread)
    m_visualExecutionOrder.clear();
    m_audioGraph.clear();

    for (Operator* op : m_executionOrder) {
        std::string name = getName(op);

        if (op->outputKind() == OutputKind::Audio) {
            // Add to AudioGraph for pull-based processing
            AudioOperator* audioOp = static_cast<AudioOperator*>(op);
            m_audioGraph.addOperator(name, audioOp);

            // Check if this is the AudioOutput
            if (name == m_audioOutputName) {
                m_audioOutput = dynamic_cast<AudioOutput*>(op);
            }
        } else {
            // Visual operator - process on main thread
            m_visualExecutionOrder.push_back(op);
        }
    }

    // Build audio graph execution order
    m_audioGraph.buildExecutionOrder();

    // Set the audio graph output
    if (!m_audioOutputName.empty()) {
        AudioOperator* audioOut = static_cast<AudioOperator*>(getByName(m_audioOutputName));
        if (audioOut) {
            m_audioGraph.setOutput(audioOut);
        }
    }

    // Connect AudioOutput to the AudioGraph for pull-based generation
    if (m_audioOutput) {
        m_audioOutput->setAudioGraph(&m_audioGraph);
    }

    // Auto-register all operators for visualization
    for (Operator* op : m_executionOrder) {
        std::string name = getName(op);
        if (!name.empty()) {
            ctx.registerOperator(name, op);
        }
    }

    m_initialized = true;
    m_lastAudioTime = 0.0;
    m_audioSamplesOwed = 0.0;

    std::cout << "[Chain] Initialized: " << m_visualExecutionOrder.size() << " visual operators, "
              << m_audioGraph.operatorCount() << " audio operators (pull-based)" << std::endl;
}

void Chain::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    if (hasError()) {
        ctx.setError(m_error);
        return;
    }

    // Debug: Log processing start (only on first frame to avoid spam)
    static bool firstDebugFrame = true;
    if (m_debug && firstDebugFrame) {
        std::cout << "\n[Chain Debug] === Processing Chain ===" << std::endl;
        std::cout << "[Chain Debug] Designated output: " << (m_outputName.empty() ? "(none)" : m_outputName) << std::endl;
    }

    // Begin GPU frame - create shared command encoder for all operators
    // This batches all GPU work into a single command buffer to reduce driver overhead
    ctx.beginGpuFrame();

    // Process ONLY visual operators on main thread
    // Audio operators are processed by AudioGraph on the audio thread
    for (Operator* op : m_visualExecutionOrder) {
        if (!op->isBypassed()) {
            // Debug logging
            if (m_debug && firstDebugFrame) {
                std::string opName = getName(op);
                std::string opType = op->name();
                WGPUTexture tex = op->outputTexture();

                std::cout << "[Chain Debug] " << opName << " (" << opType << ")";

                if (tex) {
                    // Get texture dimensions if available
                    std::cout << " -> texture";
                }

                // Check if this is the output
                if (opName == m_outputName) {
                    std::cout << " -> SCREEN OUTPUT";
                }

                std::cout << std::endl;
            }

            op->process(ctx);
        }
    }

    // End GPU frame - submit the batched command buffer
    ctx.endGpuFrame();

    if (m_debug && firstDebugFrame) {
        std::cout << "[Chain Debug] === End Processing ===" << std::endl << std::endl;
        firstDebugFrame = false;
    }

    // AudioOutput::process() handles auto-start of audio playback
    // It no longer generates audio - that happens in the miniaudio callback
    if (m_audioOutput) {
        m_audioOutput->process(ctx);
    }

    // Set output texture if specified via chain.output()
    // Use effectiveOutputView() to respect bypass chain
    if (!m_outputName.empty()) {
        Operator* output = getByName(m_outputName);
        if (output) {
            WGPUTextureView view = output->effectiveOutputView();
            if (view) {
                ctx.setOutputTexture(view);
            }
        }
    }
}

std::map<std::string, std::unique_ptr<OperatorState>> Chain::saveAllStates() {
    std::map<std::string, std::unique_ptr<OperatorState>> states;

    for (const auto& [name, op] : m_operators) {
        auto state = op->saveState();
        if (state) {
            states[name] = std::move(state);
        }
    }

    return states;
}

void Chain::restoreAllStates(std::map<std::string, std::unique_ptr<OperatorState>>& states) {
    for (auto& [name, state] : states) {
        auto it = m_operators.find(name);
        if (it != m_operators.end() && state) {
            it->second->loadState(std::move(state));
        }
    }
}

ResourceStats Chain::getResourceStats() const {
    ResourceStats stats;
    stats.operatorCount = static_cast<uint32_t>(m_operators.size());

    for (const auto& [name, op] : m_operators) {
        OutputKind kind = op->outputKind();

        if (kind == OutputKind::Texture) {
            stats.textureOperatorCount++;

            // Check if operator has an output texture
            WGPUTexture tex = op->outputTexture();
            if (tex) {
                stats.textureCount++;

                // Estimate texture memory:
                // Get dimensions from the operator (most TextureOperators have m_width/m_height)
                // Default to 1280x720 if unknown, RGBA16Float format = 8 bytes/pixel
                int width = 1280;
                int height = 720;

                // Try to get actual dimensions via reflection or known method
                // For now, use default estimate
                // RGBA16Float = 8 bytes per pixel
                size_t bytesPerPixel = 8;
                stats.estimatedTextureBytes += width * height * bytesPerPixel;
            }
        } else if (kind == OutputKind::Audio) {
            stats.audioOperatorCount++;
        }
    }

    return stats;
}

std::string ResourceStats::toString() const {
    std::string result;

    // Format: "12 operators (8 texture, 2 audio), ~64 MB texture memory"
    result += std::to_string(operatorCount) + " operators";

    if (textureOperatorCount > 0 || audioOperatorCount > 0) {
        result += " (";
        bool first = true;
        if (textureOperatorCount > 0) {
            result += std::to_string(textureOperatorCount) + " texture";
            first = false;
        }
        if (audioOperatorCount > 0) {
            if (!first) result += ", ";
            result += std::to_string(audioOperatorCount) + " audio";
        }
        result += ")";
    }

    if (estimatedTextureBytes > 0) {
        result += ", ~";
        if (estimatedTextureBytes >= 1024 * 1024) {
            result += std::to_string(estimatedTextureBytes / (1024 * 1024)) + " MB";
        } else if (estimatedTextureBytes >= 1024) {
            result += std::to_string(estimatedTextureBytes / 1024) + " KB";
        } else {
            result += std::to_string(estimatedTextureBytes) + " B";
        }
        result += " texture memory";
    }

    return result;
}

} // namespace vivid
