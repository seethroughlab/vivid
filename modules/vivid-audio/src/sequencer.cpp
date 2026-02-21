#include <vivid/audio/sequencer.h>
#include <vivid/audio/clock.h>
#include <vivid/audio/midi_receiver.h>
#include <vivid/audio/midi_sender.h>
#include <vivid/operator_registry.h>
#include <vivid/audio_graph.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <iostream>

namespace vivid::audio {

REGISTER_OPERATOR(Sequencer, "Audio Sequencing", "Step sequencer for triggering events", false);

const Step Sequencer::s_defaultStep = {};

// -----------------------------------------------------------------------------
// PRNG (xorshift32, audio-thread-safe)
// -----------------------------------------------------------------------------

float Sequencer::randomFloat() {
    m_rngState ^= m_rngState << 13;
    m_rngState ^= m_rngState >> 17;
    m_rngState ^= m_rngState << 5;
    return static_cast<float>(m_rngState) / static_cast<float>(UINT32_MAX);
}

// -----------------------------------------------------------------------------
// Condition evaluation
// -----------------------------------------------------------------------------

bool Sequencer::evaluateCondition(StepCondition cond, uint16_t cycle) const {
    switch (cond) {
        case StepCondition::Always:      return true;
        case StepCondition::OneInTwo:    return (cycle % 2) == 0;
        case StepCondition::TwoInThree:  return (cycle % 3) != 2;
        case StepCondition::OneInThree:  return (cycle % 3) == 0;
        case StepCondition::ThreeInFour: return (cycle % 4) != 3;
        case StepCondition::OneInFour:   return (cycle % 4) == 0;
        case StepCondition::OneInFive:   return (cycle % 5) == 0;
        case StepCondition::OneInSix:    return (cycle % 6) == 0;
        case StepCondition::OneInSeven:  return (cycle % 7) == 0;
        case StepCondition::OneInEight:  return (cycle % 8) == 0;
        case StepCondition::FirstOnly:   return cycle == 0;
        case StepCondition::NotFirst:    return cycle > 0;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Init / Process / Cleanup
// -----------------------------------------------------------------------------

void Sequencer::init(Context& ctx) {
    if (!beginInit()) return;

    // Allocate minimal output buffer (Sequencer doesn't produce audio samples)
    allocateOutput(256, 2, AUDIO_SAMPLE_RATE);

    // Cache chain pointer for MIDI routing lookups
    m_chain = &ctx.chain();

    // Don't clear pattern - it may have been set before init()
    // Reset playback position
    m_currentStep.store(-1, std::memory_order_relaxed);
    m_triggeredFlag.store(false, std::memory_order_relaxed);
    m_visualTriggeredFlag.store(false, std::memory_order_relaxed);
    m_currentVelocity.store(0.0f, std::memory_order_relaxed);
    m_currentNote.store(60, std::memory_order_relaxed);
    m_pendingTrigger = false;
    m_noteIsPlaying = false;
    m_slideActive = false;
    m_previousStep = -1;
    m_retrigRemaining = 0;
    m_noteOffCountdown = -1;
    m_microTimingDelaySamples = 0;
    m_microTimingPending = false;

    // Clear condition cycles
    m_conditionCycle.fill(0);

    // Sync with trigger source's current count to avoid triggering all past beats
    Operator* trigSource = triggerSource();
    if (trigSource) {
        if (auto* clock = dynamic_cast<Clock*>(trigSource)) {
            m_lastTriggerCount = clock->triggerCount();
        }
    } else {
        m_lastTriggerCount = 0;
    }

    // Resolve MIDI targets (if set before init)
    resolveTargets();
}

void Sequencer::process(Context& ctx) {
    // Main thread process() is a no-op for Sequencer
    // All timing happens in generateBlock() on the audio thread
}

void Sequencer::generateBlock(uint32_t frameCount) {
    // Called on audio thread each block

    // Clear audio-thread trigger flag at start of block
    m_triggeredFlag.store(false, std::memory_order_release);

    // Estimate step duration in samples for gate/retrig calculations
    // Use Clock trigger rate if available, otherwise assume 120 BPM / 16th notes
    int stepDurationSamples = AUDIO_SAMPLE_RATE / 8;  // Default: 120 BPM 16ths
    Operator* trigSource = triggerSource();
    if (trigSource) {
        if (auto* clock = dynamic_cast<Clock*>(trigSource)) {
            float bpm = static_cast<float>(clock->bpm);
            if (bpm > 0.0f) {
                // Clock trigger interval depends on division, but we can estimate
                // from trigger count rate. For now use the clock's period.
                float beatsPerSec = bpm / 60.0f;
                // Assume each trigger = one step
                stepDurationSamples = static_cast<int>(AUDIO_SAMPLE_RATE / beatsPerSec / 4.0f);
                if (stepDurationSamples < 1) stepDurationSamples = 1;
            }
        }
    }

    // --- Process pending note-off (gate timing) ---
    if (m_noteOffCountdown >= 0) {
        m_noteOffCountdown -= static_cast<int>(frameCount);
        if (m_noteOffCountdown <= 0) {
            m_noteOffCountdown = -1;
            if (m_noteIsPlaying && !m_slideActive && (m_cachedTarget || m_cachedMidiOut)) {
                sendNoteOff(m_lastPlayedNote);
                m_noteIsPlaying = false;
            }
        }
    }

    // --- Process retrigs ---
    if (m_retrigRemaining > 0) {
        m_retrigCountdown -= static_cast<int>(frameCount);
        if (m_retrigCountdown <= 0) {
            // Fire retrig
            if (m_cachedTarget || m_cachedMidiOut) {
                sendNoteOn(m_retrigNote, m_retrigVelocity);
                m_lastPlayedNote = m_retrigNote;
                m_noteIsPlaying = true;
            }
            m_triggeredFlag.store(true, std::memory_order_release);
            m_visualTriggeredFlag.store(true, std::memory_order_release);
            if (m_onStepVel) m_onStepVel(m_retrigVelocity);
            if (m_onStepSimple) m_onStepSimple();

            m_retrigRemaining--;
            if (m_retrigRemaining > 0) {
                m_retrigCountdown += m_retrigIntervalSamples;
            }
        }
    }

    // --- Process micro-timing delayed trigger ---
    if (m_microTimingPending) {
        m_microTimingDelaySamples -= static_cast<int>(frameCount);
        if (m_microTimingDelaySamples <= 0) {
            m_microTimingPending = false;
            fireStep(m_pendingMicroStep, stepDurationSamples);
        }
    }

    // Track if a trigger happens this block
    bool triggeredThisBlock = false;

    // Check if our trigger source (e.g., Clock) has triggered
    if (trigSource) {
        if (auto* clock = dynamic_cast<Clock*>(trigSource)) {
            uint64_t currentCount = clock->triggerCount();
            if (currentCount > m_lastTriggerCount) {
                uint64_t triggers = currentCount - m_lastTriggerCount;
                for (uint64_t i = 0; i < triggers; ++i) {
                    advanceInternalNoFlag();
                    int current = m_currentStep.load(std::memory_order_relaxed);
                    const Step& s = m_steps[current];
                    if (s.active) {
                        triggeredThisBlock = true;
                    }
                }
                m_lastTriggerCount = currentCount;
            }
        }
        else if (auto* seq = dynamic_cast<Sequencer*>(trigSource)) {
            if (seq->triggered()) {
                advanceInternalNoFlag();
                int current = m_currentStep.load(std::memory_order_relaxed);
                if (m_steps[current].active) {
                    triggeredThisBlock = true;
                }
            }
        }
    }

    // If we have a pending trigger (from midiNoteOn or external trigger() call)
    if (m_pendingTrigger) {
        m_pendingTrigger = false;
        advanceInternalNoFlag();
        int current = m_currentStep.load(std::memory_order_relaxed);
        if (m_steps[current].active) {
            triggeredThisBlock = true;
        }
    }

    // Process the active step
    if (triggeredThisBlock) {
        int current = m_currentStep.load(std::memory_order_relaxed);
        const Step& s = m_steps[current];

        // Increment condition cycle for this step
        uint16_t cycle = m_conditionCycle[current];
        m_conditionCycle[current]++;

        // Evaluate probability
        bool probPass = (s.probability >= 1.0f) || (randomFloat() < s.probability);

        // Evaluate condition
        bool condPass = evaluateCondition(s.condition, cycle);

        if (probPass && condPass) {
            // Check for micro-timing offset
            if (s.microTiming > 0.01f) {
                // Positive = late, delay the trigger
                m_microTimingDelaySamples = static_cast<int>(s.microTiming * stepDurationSamples);
                m_microTimingPending = true;
                m_pendingMicroStep = s;
            } else {
                // Negative micro-timing or zero: fire immediately
                // (negative = early, but we fire at block boundary which is close enough)
                fireStep(s, stepDurationSamples);
            }
        }
    }

    // Resize output buffer if needed (even though we don't produce audio)
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }
}

void Sequencer::fireStep(const Step& s, int stepDurationSamples) {
    m_currentVelocity.store(s.velocity, std::memory_order_relaxed);
    m_currentNote.store(s.note, std::memory_order_relaxed);
    m_triggeredFlag.store(true, std::memory_order_release);
    m_visualTriggeredFlag.store(true, std::memory_order_release);

    // Handle slide: check if previous step had slide=true
    bool prevSlide = false;
    if (m_previousStep >= 0 && m_previousStep < MAX_STEPS) {
        prevSlide = m_steps[m_previousStep].slide;
    }

    // Send MIDI
    if (m_cachedTarget || m_cachedMidiOut) {
        if (m_noteIsPlaying && !prevSlide) {
            sendNoteOff(m_lastPlayedNote);
        }
        sendNoteOn(s.note, s.velocity);
        m_lastPlayedNote = s.note;
        m_noteIsPlaying = true;
        m_slideActive = s.slide;

        // Per-step gate: schedule note-off
        float gateValue = (s.gate >= 0.0f) ? s.gate : static_cast<float>(gate);
        m_noteOffCountdown = static_cast<int>(gateValue * stepDurationSamples);

        // Per-step CC
        for (const auto& ccEntry : s.cc) {
            if (ccEntry.value >= 0.0f) {
                sendCC(ccEntry.cc, ccEntry.value);
            }
        }
    }

    // Schedule retrigs
    if (s.retrigCount > 0) {
        m_retrigRemaining = s.retrigCount;
        m_retrigIntervalSamples = static_cast<int>(s.retrigRate * stepDurationSamples);
        if (m_retrigIntervalSamples < 1) m_retrigIntervalSamples = 1;
        m_retrigCountdown = m_retrigIntervalSamples;
        m_retrigVelocity = s.velocity;
        m_retrigNote = s.note;
    }

    m_previousStep = m_currentStep.load(std::memory_order_relaxed);

    // Invoke step callbacks
    if (m_onStepVel) {
        m_onStepVel(s.velocity);
    }
    if (m_onStepSimple) {
        m_onStepSimple();
    }
}

void Sequencer::midiNoteOn(uint8_t /*note*/, float /*velocity*/, uint8_t /*channel*/) {
    // MIDI note-on advances the step (same as trigger)
    Operator* src = triggerSource();
    if (src && dynamic_cast<Clock*>(src)) {
        return;  // Clock timing handled internally via triggerCount polling
    }
    m_pendingTrigger = true;
}

void Sequencer::midiNoteOff(uint8_t /*note*/, float /*velocity*/, uint8_t /*channel*/) {
    // Nothing to do on note-off for step advance
}

void Sequencer::advanceInternalNoFlag() {
    int numSteps = static_cast<int>(steps);
    if (numSteps < 1) numSteps = 1;
    if (numSteps > MAX_STEPS) numSteps = MAX_STEPS;

    int current = m_currentStep.load(std::memory_order_relaxed);
    current = (current + 1) % numSteps;
    m_currentStep.store(current, std::memory_order_relaxed);
}

void Sequencer::advanceInternal() {
    advanceInternalNoFlag();

    int current = m_currentStep.load(std::memory_order_relaxed);
    const Step& s = m_steps[current];
    float velocity = s.active ? s.velocity : 0.0f;

    m_currentVelocity.store(velocity, std::memory_order_relaxed);
    m_triggeredFlag.store(s.active, std::memory_order_release);
}

void Sequencer::cleanup() {
    // Send note-off for any playing note before cleanup
    if (m_noteIsPlaying && (m_cachedTarget || m_cachedMidiOut)) {
        sendNoteOff(m_lastPlayedNote);
        m_noteIsPlaying = false;
    }

    m_chain = nullptr;
    m_cachedTarget = nullptr;
    m_cachedMidiOut = nullptr;
    resetInit();
    releaseOutput();
}

// -----------------------------------------------------------------------------
// Pattern Editing
// -----------------------------------------------------------------------------

void Sequencer::setStep(int index, const Step& s) {
    if (index >= 0 && index < MAX_STEPS) {
        m_steps[index] = s;
        m_steps[index].active = true;  // Always activate when using Step overload
    }
}

void Sequencer::setStep(int step, bool on, float velocity) {
    if (step >= 0 && step < MAX_STEPS) {
        m_steps[step].active = on;
        m_steps[step].velocity = velocity;
    }
}

void Sequencer::setStep(int step, uint8_t note, float velocity) {
    if (step >= 0 && step < MAX_STEPS) {
        m_steps[step].active = true;
        m_steps[step].note = note;
        m_steps[step].velocity = velocity;
    }
}

void Sequencer::setSteps(std::initializer_list<int> activeSteps) {
    clearPattern();
    for (int idx : activeSteps) {
        if (idx >= 0 && idx < MAX_STEPS) {
            m_steps[idx].active = true;
        }
    }
}

const Step& Sequencer::step(int index) const {
    if (index >= 0 && index < MAX_STEPS) {
        return m_steps[index];
    }
    return s_defaultStep;
}

bool Sequencer::isActive(int index) const {
    if (index >= 0 && index < MAX_STEPS) {
        return m_steps[index].active;
    }
    return false;
}

bool Sequencer::getStep(int step) const {
    if (step >= 0 && step < MAX_STEPS) {
        return m_steps[step].active;
    }
    return false;
}

float Sequencer::getVelocity(int step) const {
    if (step >= 0 && step < MAX_STEPS) {
        return m_steps[step].velocity;
    }
    return 0.0f;
}

uint8_t Sequencer::getNote(int step) const {
    if (step >= 0 && step < MAX_STEPS) {
        return m_steps[step].note;
    }
    return 60;
}

void Sequencer::clearPattern() {
    // Send note-off for any playing note
    if (m_noteIsPlaying && (m_cachedTarget || m_cachedMidiOut)) {
        sendNoteOff(m_lastPlayedNote);
        m_noteIsPlaying = false;
    }

    for (int i = 0; i < MAX_STEPS; ++i) {
        m_steps[i] = Step{};  // Reset to defaults
    }
}

void Sequencer::advance() {
    int numSteps = static_cast<int>(steps);
    if (numSteps < 1) numSteps = 1;
    if (numSteps > MAX_STEPS) numSteps = MAX_STEPS;

    int current = m_currentStep.load(std::memory_order_relaxed);
    current = (current + 1) % numSteps;
    m_currentStep.store(current, std::memory_order_relaxed);

    const Step& s = m_steps[current];
    float velocity = s.active ? s.velocity : 0.0f;

    m_currentVelocity.store(velocity, std::memory_order_relaxed);
    m_triggeredFlag.store(s.active, std::memory_order_release);
    m_visualTriggeredFlag.store(s.active, std::memory_order_release);
}

void Sequencer::reset() {
    // Send note-off for any playing note
    if (m_noteIsPlaying && (m_cachedTarget || m_cachedMidiOut)) {
        sendNoteOff(m_lastPlayedNote);
        m_noteIsPlaying = false;
    }

    m_currentStep.store(-1, std::memory_order_relaxed);
    m_triggeredFlag.store(false, std::memory_order_relaxed);
    m_visualTriggeredFlag.store(false, std::memory_order_relaxed);
    m_currentVelocity.store(0.0f, std::memory_order_relaxed);
    m_currentNote.store(60, std::memory_order_relaxed);
    m_pendingTrigger = false;
    m_lastTriggerCount = 0;
    m_slideActive = false;
    m_previousStep = -1;
    m_retrigRemaining = 0;
    m_noteOffCountdown = -1;
    m_microTimingDelaySamples = 0;
    m_microTimingPending = false;
    m_conditionCycle.fill(0);
}

// -----------------------------------------------------------------------------
// MIDI Routing
// -----------------------------------------------------------------------------

void Sequencer::setTarget(const std::string& targetName) {
    m_targetName = targetName;
    m_cachedTarget = nullptr;
    resolveTargets();
}

void Sequencer::clearTarget() {
    if (m_noteIsPlaying && m_cachedTarget) {
        m_cachedTarget->midiNoteOff(m_lastPlayedNote, 0.0f, static_cast<uint8_t>(static_cast<int>(midiChannel)));
    }
    m_targetName.clear();
    m_cachedTarget = nullptr;
}

void Sequencer::setMidiOutput(const std::string& midiOutName) {
    m_midiOutName = midiOutName;
    m_cachedMidiOut = nullptr;
    resolveTargets();
}

void Sequencer::clearMidiOutput() {
    if (m_noteIsPlaying && m_cachedMidiOut) {
        m_cachedMidiOut->sendNoteOff(static_cast<uint8_t>(static_cast<int>(midiChannel)), m_lastPlayedNote);
    }
    m_midiOutName.clear();
    m_cachedMidiOut = nullptr;
}

void Sequencer::resolveTargets() {
    if (!m_chain) return;

    if (!m_targetName.empty() && !m_cachedTarget) {
        Operator* op = m_chain->getByName(m_targetName);
        if (op) {
            m_cachedTarget = dynamic_cast<MidiReceiver*>(op);
            if (!m_cachedTarget) {
                std::cerr << "Sequencer: Target '" << m_targetName
                          << "' does not implement MidiReceiver" << std::endl;
            }
        }
    }

    if (!m_midiOutName.empty() && !m_cachedMidiOut) {
        Operator* op = m_chain->getByName(m_midiOutName);
        if (op) {
            m_cachedMidiOut = dynamic_cast<MidiSender*>(op);
            if (!m_cachedMidiOut) {
                std::cerr << "Sequencer: MidiOut '" << m_midiOutName
                          << "' does not implement MidiSender" << std::endl;
            }
        }
    }
}

void Sequencer::sendNoteOn(uint8_t note, float velocity) {
    uint8_t channel = static_cast<uint8_t>(static_cast<int>(midiChannel));

    if ((!m_cachedTarget && !m_targetName.empty()) ||
        (!m_cachedMidiOut && !m_midiOutName.empty())) {
        resolveTargets();
    }

    if (m_cachedTarget) {
        m_cachedTarget->midiNoteOn(note, velocity, channel);
    }

    if (m_cachedMidiOut) {
        m_cachedMidiOut->sendNoteOn(channel, note, velocity);
    }
}

void Sequencer::sendNoteOff(uint8_t note) {
    uint8_t channel = static_cast<uint8_t>(static_cast<int>(midiChannel));

    if (m_cachedTarget) {
        m_cachedTarget->midiNoteOff(note, 0.0f, channel);
    }

    if (m_cachedMidiOut) {
        m_cachedMidiOut->sendNoteOff(channel, note);
    }
}

void Sequencer::sendCC(uint8_t cc, float value) {
    uint8_t channel = static_cast<uint8_t>(static_cast<int>(midiChannel));

    if ((!m_cachedTarget && !m_targetName.empty()) ||
        (!m_cachedMidiOut && !m_midiOutName.empty())) {
        resolveTargets();
    }

    if (m_cachedTarget) {
        m_cachedTarget->midiCC(cc, value, channel);
    }

    if (m_cachedMidiOut) {
        m_cachedMidiOut->sendCC(channel, cc, value);
    }
}

InspectData Sequencer::inspect() const {
    auto data = Operator::inspect();
    data.set("current_step", static_cast<float>(currentStep()));
    data.set("current_velocity", currentVelocity());
    data.set("current_note", static_cast<float>(currentNote()));
    int activeSteps = 0;
    for (int i = 0; i < static_cast<int>(steps); ++i) {
        if (m_steps[i].active) activeSteps++;
    }
    data.set("active_steps", static_cast<float>(activeSteps));
    return data;
}

} // namespace vivid::audio
