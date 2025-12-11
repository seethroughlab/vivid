// Vivid - Chain Implementation

#include <vivid/chain.h>
#include <vivid/context.h>
#include <vivid/operator.h>
#include <vivid/audio_operator.h>
#include <vivid/audio_output.h>
#include <vivid/audio_buffer.h>
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <iostream>
#include <functional>
#include <cstring>

namespace vivid {

Operator* Chain::addOperator(const std::string& name, Operator* op) {
    // Take ownership of the operator using unique_ptr
    // This function lives in the exe, so the unique_ptr uses the exe's allocator
    operators_[name] = std::unique_ptr<Operator>(op);
    orderedNames_.push_back(name);
    operatorNames_[op] = name;
    needsSort_ = true;
    return op;
}

Operator* Chain::getByName(const std::string& name) {
    auto it = operators_.find(name);
    if (it == operators_.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::string Chain::getName(Operator* op) const {
    auto it = operatorNames_.find(op);
    if (it == operatorNames_.end()) {
        return "";
    }
    return it->second;
}

void Chain::setOutput(Operator* op) {
    std::string name = getName(op);
    if (!name.empty()) {
        outputName_ = name;
    }
}

Operator* Chain::getOutput() const {
    if (outputName_.empty()) {
        return nullptr;
    }
    auto it = operators_.find(outputName_);
    return it != operators_.end() ? it->second.get() : nullptr;
}

void Chain::audioOutput(const std::string& name) {
    Operator* op = getByName(name);
    if (!op) {
        error_ = "Audio output operator '" + name + "' not found";
        return;
    }
    if (op->outputKind() != OutputKind::Audio) {
        error_ = "Audio output operator must produce audio. '" + name + "' produces " +
                 outputKindName(op->outputKind()) + ".";
        return;
    }
    audioOutputName_ = name;
}

Operator* Chain::getAudioOutput() const {
    if (audioOutputName_.empty()) {
        return nullptr;
    }
    auto it = operators_.find(audioOutputName_);
    return it != operators_.end() ? it->second.get() : nullptr;
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
    if (!audioOutput_) {
        std::memset(output, 0, frameCount * AUDIO_CHANNELS * sizeof(float));
        return;
    }
    audioOutput_->generateForExport(output, frameCount);
}

void Chain::buildDependencyGraph() {
    // For each operator, find which other operators it depends on
    // by looking at its inputs
    for (const auto& [name, op] : operators_) {
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

    for (const auto& [name, op] : operators_) {
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

    for (const auto& [name, op] : operators_) {
        if (colors[op.get()] == Color::White) {
            if (hasCycle(op.get())) {
                return true;
            }
        }
    }

    return false;
}

void Chain::computeExecutionOrder() {
    if (!needsSort_) {
        return;
    }

    executionOrder_.clear();
    error_.clear();

    // Check for cycles first
    if (detectCycle()) {
        error_ = "Circular dependency detected in operator chain";
        return;
    }

    // Kahn's algorithm for topological sort
    // Count incoming edges (dependencies) for each operator
    std::unordered_map<Operator*, int> inDegree;
    std::unordered_map<Operator*, std::vector<Operator*>> dependents;

    for (const auto& [name, op] : operators_) {
        inDegree[op.get()] = 0;
    }

    // Build reverse dependency graph and count in-degrees
    for (const auto& [name, op] : operators_) {
        for (size_t i = 0; i < op->inputCount(); ++i) {
            Operator* input = op->getInput(i);
            if (input && operators_.count(getName(input))) {
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
        executionOrder_.push_back(current);

        // Reduce in-degree for all dependents
        for (Operator* dependent : dependents[current]) {
            inDegree[dependent]--;
            if (inDegree[dependent] == 0) {
                ready.push(dependent);
            }
        }
    }

    // Check if all operators were processed
    if (executionOrder_.size() != operators_.size()) {
        error_ = "Could not resolve operator dependencies (possible cycle)";
        return;
    }

    needsSort_ = false;
}

void Chain::init(Context& ctx) {
    // First pass: call init on all operators to resolve named inputs
    // This must happen before computeExecutionOrder() so the topological
    // sort can see the actual dependencies
    for (const auto& [name, op] : operators_) {
        op->init(ctx);
    }

    // Now compute execution order with resolved dependencies
    computeExecutionOrder();

    if (hasError()) {
        ctx.setError(error_);
        return;
    }

    // Separate audio and visual operators
    // Audio operators go to AudioGraph (processed on audio thread)
    // Visual operators stay in visualExecutionOrder_ (processed on main thread)
    visualExecutionOrder_.clear();
    audioGraph_.clear();

    for (Operator* op : executionOrder_) {
        std::string name = getName(op);

        if (op->outputKind() == OutputKind::Audio) {
            // Add to AudioGraph for pull-based processing
            AudioOperator* audioOp = static_cast<AudioOperator*>(op);
            audioGraph_.addOperator(name, audioOp);

            // Check if this is the AudioOutput
            if (name == audioOutputName_) {
                audioOutput_ = dynamic_cast<AudioOutput*>(op);
            }
        } else {
            // Visual operator - process on main thread
            visualExecutionOrder_.push_back(op);
        }
    }

    // Build audio graph execution order
    audioGraph_.buildExecutionOrder();

    // Set the audio graph output
    if (!audioOutputName_.empty()) {
        AudioOperator* audioOut = static_cast<AudioOperator*>(getByName(audioOutputName_));
        if (audioOut) {
            audioGraph_.setOutput(audioOut);
        }
    }

    // Connect AudioOutput to the AudioGraph for pull-based generation
    if (audioOutput_) {
        audioOutput_->setAudioGraph(&audioGraph_);
    }

    // Auto-register all operators for visualization
    for (Operator* op : executionOrder_) {
        std::string name = getName(op);
        if (!name.empty()) {
            ctx.registerOperator(name, op);
        }
    }

    initialized_ = true;
    lastAudioTime_ = 0.0;
    audioSamplesOwed_ = 0.0;

    std::cout << "[Chain] Initialized: " << visualExecutionOrder_.size() << " visual operators, "
              << audioGraph_.operatorCount() << " audio operators (pull-based)" << std::endl;
}

void Chain::process(Context& ctx) {
    if (!initialized_) {
        init(ctx);
    }

    if (hasError()) {
        ctx.setError(error_);
        return;
    }

    // Process ONLY visual operators on main thread
    // Audio operators are processed by AudioGraph on the audio thread
    for (Operator* op : visualExecutionOrder_) {
        if (!op->isBypassed()) {
            op->process(ctx);
        }
    }

    // AudioOutput::process() handles auto-start of audio playback
    // It no longer generates audio - that happens in the miniaudio callback
    if (audioOutput_) {
        audioOutput_->process(ctx);
    }

    // Set output texture if specified via chain.output()
    // Use effectiveOutputView() to respect bypass chain
    if (!outputName_.empty()) {
        Operator* output = getByName(outputName_);
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

    for (const auto& [name, op] : operators_) {
        auto state = op->saveState();
        if (state) {
            states[name] = std::move(state);
        }
    }

    return states;
}

void Chain::restoreAllStates(std::map<std::string, std::unique_ptr<OperatorState>>& states) {
    for (auto& [name, state] : states) {
        auto it = operators_.find(name);
        if (it != operators_.end() && state) {
            it->second->loadState(std::move(state));
        }
    }
}

} // namespace vivid
