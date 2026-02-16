/**
 * @file audio_graph.cpp
 * @brief Implementation of pull-based audio processing graph
 */

#include <vivid/audio_graph.h>
#include <vivid/audio_operator.h>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <queue>
#include <unordered_set>

namespace vivid {

uint32_t AudioGraph::addOperator(const std::string& name, AudioOperator* op) {
    uint32_t id = static_cast<uint32_t>(m_operators.size());
    m_operators.push_back({name, op});
    m_nameToId[name] = id;

    // Set the audio graph pointer and operator ID
    op->setAudioGraph(this, id);

    return id;
}

AudioOperator* AudioGraph::getOperator(const std::string& name) {
    auto it = m_nameToId.find(name);
    if (it != m_nameToId.end() && it->second < m_operators.size()) {
        return m_operators[it->second].op;
    }
    return nullptr;
}

uint32_t AudioGraph::getOperatorId(const std::string& name) const {
    auto it = m_nameToId.find(name);
    if (it != m_nameToId.end()) {
        return it->second;
    }
    return UINT32_MAX;
}

void AudioGraph::setOutput(AudioOperator* op) {
    m_output = op;
}

void AudioGraph::buildExecutionOrder() {
    m_executionOrder.clear();
    m_hasCycles = false;
    m_cycleWarning.clear();

    // Build a set of operators in this graph for quick lookup
    std::unordered_set<Operator*> graphOps;
    for (auto& entry : m_operators) {
        if (entry.op) {
            graphOps.insert(entry.op);
        }
    }

    // Build adjacency: for each operator, collect edges from audio inputs
    // and trigger sources. Edge: dependency -> dependent (source -> consumer).
    std::unordered_map<Operator*, std::vector<Operator*>> dependents;
    std::unordered_map<Operator*, int> inDegree;

    for (auto& entry : m_operators) {
        if (!entry.op) continue;
        inDegree[entry.op] = 0;
    }

    for (auto& entry : m_operators) {
        if (!entry.op) continue;
        Operator* op = entry.op;

        // Audio input edges
        for (size_t i = 0; i < op->inputCount(); ++i) {
            Operator* input = op->getInput(i);
            if (input && graphOps.count(input)) {
                inDegree[op]++;
                dependents[input].push_back(op);
            }
        }

        // Trigger source edge
        Operator* trigSrc = op->triggerSource();
        if (trigSrc && graphOps.count(trigSrc)) {
            inDegree[op]++;
            dependents[trigSrc].push_back(op);
        }
    }

    // Kahn's algorithm
    std::queue<Operator*> ready;
    for (auto& entry : m_operators) {
        if (entry.op && inDegree[entry.op] == 0) {
            ready.push(entry.op);
        }
    }

    std::unordered_set<Operator*> sorted;
    while (!ready.empty()) {
        Operator* current = ready.front();
        ready.pop();
        m_executionOrder.push_back(static_cast<AudioOperator*>(current));
        sorted.insert(current);

        for (Operator* dep : dependents[current]) {
            inDegree[dep]--;
            if (inDegree[dep] == 0) {
                ready.push(dep);
            }
        }
    }

    // Handle cycles: append any remaining operators in insertion order
    // Audio feedback loops are valid, so we don't error — just warn
    if (sorted.size() < graphOps.size()) {
        m_hasCycles = true;
        m_cycleWarning = "Audio graph contains cycles; cyclic operators use insertion order";
        for (auto& entry : m_operators) {
            if (entry.op && sorted.find(entry.op) == sorted.end()) {
                m_executionOrder.push_back(entry.op);
            }
        }
    }
}

void AudioGraph::clear() {
    m_operators.clear();
    m_executionOrder.clear();
    m_nameToId.clear();
    m_output = nullptr;
    m_hasCycles = false;
    m_cycleWarning.clear();
}

void AudioGraph::processBlock(float* output, uint32_t frameCount) {
    using Clock = std::chrono::high_resolution_clock;
    auto start = Clock::now();

    // 1. Process queued events from main thread
    processEvents();

    // 2. Generate audio from all operators in execution order
    //    Operators must be in dependency order (sources before targets)
    //    For each operator: check trigger source, then generate block
    for (AudioOperator* op : m_executionOrder) {
        if (!op) continue;

        // Check if this operator's trigger source fired
        // The source was processed earlier in this loop (dependency order)
        if (Operator* src = op->triggerSource()) {
            if (src->triggered()) {
                // Send trigger event with velocity from source
                AudioEvent triggerEvent;
                triggerEvent.type = AudioEventType::Trigger;
                triggerEvent.value1 = src->triggerVelocity();
                op->handleEvent(triggerEvent);
            }
        }

        op->generateBlock(frameCount);
    }

    // 3. Copy output to destination buffer
    if (m_output) {
        const AudioBuffer* outBuf = m_output->outputBuffer();
        if (outBuf && outBuf->isValid()) {
            uint32_t sampleCount = std::min(frameCount * AUDIO_CHANNELS,
                                           outBuf->sampleCount());
            std::memcpy(output, outBuf->samples, sampleCount * sizeof(float));

            // Zero any remaining samples if buffer was smaller
            if (sampleCount < frameCount * AUDIO_CHANNELS) {
                std::memset(output + sampleCount, 0,
                           (frameCount * AUDIO_CHANNELS - sampleCount) * sizeof(float));
            }
        } else {
            // No valid output - silence
            std::memset(output, 0, frameCount * AUDIO_CHANNELS * sizeof(float));
        }
    } else {
        // No output operator - silence
        std::memset(output, 0, frameCount * AUDIO_CHANNELS * sizeof(float));
    }

    // 4. Measure DSP load
    auto end = Clock::now();
    double processingTime = std::chrono::duration<double>(end - start).count();
    double bufferDuration = static_cast<double>(frameCount) / AUDIO_SAMPLE_RATE;
    float load = static_cast<float>(processingTime / bufferDuration);

    // Smoothed load (exponential moving average)
    float currentLoad = m_dspLoad.load(std::memory_order_relaxed);
    float smoothedLoad = currentLoad * 0.9f + load * 0.1f;
    m_dspLoad.store(smoothedLoad, std::memory_order_relaxed);

    // Track peak
    float peak = m_peakDspLoad.load(std::memory_order_relaxed);
    if (load > peak) {
        m_peakDspLoad.store(load, std::memory_order_relaxed);
    }
}

void AudioGraph::processEvents() {
    AudioEvent event;
    while (m_eventQueue.pop(event)) {
        // Route event to target operator
        if (event.operatorId < m_operators.size()) {
            AudioOperator* op = m_operators[event.operatorId].op;
            if (op) {
                op->handleEvent(event);
            }
        }
    }
}

void AudioGraph::queueNoteOn(uint32_t operatorId, float frequency, float velocity) {
    AudioEvent event;
    event.type = AudioEventType::NoteOn;
    event.operatorId = operatorId;
    event.value1 = frequency;
    event.value2 = velocity;
    m_eventQueue.push(event);
}

void AudioGraph::queueNoteOff(uint32_t operatorId) {
    AudioEvent event;
    event.type = AudioEventType::NoteOff;
    event.operatorId = operatorId;
    m_eventQueue.push(event);
}

void AudioGraph::queueTrigger(uint32_t operatorId) {
    AudioEvent event;
    event.type = AudioEventType::Trigger;
    event.operatorId = operatorId;
    m_eventQueue.push(event);
}

void AudioGraph::queueParamChange(uint32_t operatorId, uint32_t paramId, float value) {
    AudioEvent event;
    event.type = AudioEventType::ParamChange;
    event.operatorId = operatorId;
    event.paramId = paramId;
    event.value1 = value;
    m_eventQueue.push(event);
}

void AudioGraph::queueReset(uint32_t operatorId) {
    AudioEvent event;
    event.type = AudioEventType::Reset;
    event.operatorId = operatorId;
    m_eventQueue.push(event);
}

} // namespace vivid
