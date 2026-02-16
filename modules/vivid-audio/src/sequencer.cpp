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
    // This ensures downstream audio operators only see the trigger for one block
    m_triggeredFlag.store(false, std::memory_order_release);

    // Track if a trigger happens this block
    bool triggeredThisBlock = false;

    // Check if our trigger source (e.g., Clock) has triggered
    // This allows Sequencer to advance automatically on the audio thread
    Operator* trigSource = triggerSource();
    if (trigSource) {
        // Try Clock first (most common trigger source)
        if (auto* clock = dynamic_cast<Clock*>(trigSource)) {
            uint64_t currentCount = clock->triggerCount();
            if (currentCount > m_lastTriggerCount) {
                // Clock has triggered - advance for each trigger we missed
                // Important: accumulate triggers so we don't lose active steps during catchup
                uint64_t triggers = currentCount - m_lastTriggerCount;
                for (uint64_t i = 0; i < triggers; ++i) {
                    advanceInternalNoFlag();  // Advance without setting flag
                    if (m_pattern[m_currentStep.load(std::memory_order_relaxed)]) {
                        triggeredThisBlock = true;
                    }
                }
                m_lastTriggerCount = currentCount;
            }
        }
        // Try another Sequencer
        else if (auto* seq = dynamic_cast<Sequencer*>(trigSource)) {
            // Check if the source sequencer triggered
            if (seq->triggered()) {
                advanceInternalNoFlag();
                if (m_pattern[m_currentStep.load(std::memory_order_relaxed)]) {
                    triggeredThisBlock = true;
                }
            }
        }
    }

    // If we have a pending trigger (from midiNoteOn or external trigger() call), advance the step
    if (m_pendingTrigger) {
        m_pendingTrigger = false;
        advanceInternalNoFlag();
        if (m_pattern[m_currentStep.load(std::memory_order_relaxed)]) {
            triggeredThisBlock = true;
        }
    }

    // Set triggered flags if ANY step was active during this block
    if (triggeredThisBlock) {
        int current = m_currentStep.load(std::memory_order_relaxed);
        float velocity = m_velocities[current];
        uint8_t note = m_notes[current];

        m_currentVelocity.store(velocity, std::memory_order_relaxed);
        m_currentNote.store(note, std::memory_order_relaxed);
        m_triggeredFlag.store(true, std::memory_order_release);       // For audio thread
        m_visualTriggeredFlag.store(true, std::memory_order_release); // For main thread

        // Send MIDI note-off for previous note (if any) then note-on for new note
        if (m_cachedTarget || m_cachedMidiOut) {
            if (m_noteIsPlaying) {
                sendNoteOff(m_lastPlayedNote);
            }
            sendNoteOn(note, velocity);
            m_lastPlayedNote = note;
            m_noteIsPlaying = true;
        }

        // Invoke step callbacks
        if (m_onStepVel) {
            m_onStepVel(velocity);
        }
        if (m_onStepSimple) {
            m_onStepSimple();
        }
    }

    // Resize output buffer if needed (even though we don't produce audio)
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }
}

void Sequencer::midiNoteOn(uint8_t /*note*/, float /*velocity*/, uint8_t /*channel*/) {
    // MIDI note-on advances the step (same as trigger)
    // If our trigger source is a Clock, we poll triggerCount internally
    // in generateBlock(), so ignore external trigger events to avoid double-advancing
    Operator* src = triggerSource();
    if (src && dynamic_cast<Clock*>(src)) {
        return;  // Clock timing handled internally via triggerCount polling
    }

    // For non-Clock trigger sources, set pending flag
    // We'll advance in generateBlock() to ensure the triggered flag
    // is set at a consistent point in the block
    m_pendingTrigger = true;
}

void Sequencer::midiNoteOff(uint8_t /*note*/, float /*velocity*/, uint8_t /*channel*/) {
    // Nothing to do on note-off for step advance
}

void Sequencer::advanceInternalNoFlag() {
    // Called on audio thread - advances to next step WITHOUT setting triggered flag
    // Used during catchup to avoid overwriting flag for intermediate steps
    int numSteps = static_cast<int>(steps);
    if (numSteps < 1) numSteps = 1;
    if (numSteps > MAX_STEPS) numSteps = MAX_STEPS;

    // Move to next step
    int current = m_currentStep.load(std::memory_order_relaxed);
    current = (current + 1) % numSteps;
    m_currentStep.store(current, std::memory_order_relaxed);
}

void Sequencer::advanceInternal() {
    // Called on audio thread - advances to next step and sets flag
    advanceInternalNoFlag();

    int current = m_currentStep.load(std::memory_order_relaxed);
    bool stepActive = m_pattern[current];
    float velocity = stepActive ? m_velocities[current] : 0.0f;

    m_currentVelocity.store(velocity, std::memory_order_relaxed);
    m_triggeredFlag.store(stepActive, std::memory_order_release);
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

void Sequencer::setStep(int step, bool on, float velocity) {
    if (step >= 0 && step < MAX_STEPS) {
        m_pattern[step] = on;
        m_velocities[step] = velocity;
    }
}

bool Sequencer::getStep(int step) const {
    if (step >= 0 && step < MAX_STEPS) {
        return m_pattern[step];
    }
    return false;
}

float Sequencer::getVelocity(int step) const {
    if (step >= 0 && step < MAX_STEPS) {
        return m_velocities[step];
    }
    return 0.0f;
}

void Sequencer::clearPattern() {
    // Send note-off for any playing note
    if (m_noteIsPlaying && (m_cachedTarget || m_cachedMidiOut)) {
        sendNoteOff(m_lastPlayedNote);
        m_noteIsPlaying = false;
    }

    for (int i = 0; i < MAX_STEPS; ++i) {
        m_pattern[i] = false;
        m_velocities[i] = 1.0f;
        m_notes[i] = 60;  // Reset to middle C
    }
}

void Sequencer::setPattern(uint16_t pattern) {
    for (int i = 0; i < MAX_STEPS; ++i) {
        m_pattern[i] = (pattern & (1 << i)) != 0;
    }
}

void Sequencer::advance() {
    // Backward-compatible advance: directly advance and set flag
    // This matches the original synchronous behavior for main-thread callers
    int numSteps = static_cast<int>(steps);
    if (numSteps < 1) numSteps = 1;
    if (numSteps > MAX_STEPS) numSteps = MAX_STEPS;

    // Move to next step
    int current = m_currentStep.load(std::memory_order_relaxed);
    current = (current + 1) % numSteps;
    m_currentStep.store(current, std::memory_order_relaxed);

    // Check if current step is active and set triggered flags
    bool stepActive = m_pattern[current];
    float velocity = stepActive ? m_velocities[current] : 0.0f;

    m_currentVelocity.store(velocity, std::memory_order_relaxed);
    m_triggeredFlag.store(stepActive, std::memory_order_release);
    m_visualTriggeredFlag.store(stepActive, std::memory_order_release);
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
}

// -----------------------------------------------------------------------------
// Note data
// -----------------------------------------------------------------------------

void Sequencer::setStep(int step, uint8_t note, float velocity) {
    if (step >= 0 && step < MAX_STEPS) {
        m_pattern[step] = true;  // Automatically enable step
        m_notes[step] = note;
        m_velocities[step] = velocity;
    }
}

uint8_t Sequencer::getNote(int step) const {
    if (step >= 0 && step < MAX_STEPS) {
        return m_notes[step];
    }
    return 60;  // Default middle C
}

// -----------------------------------------------------------------------------
// MIDI Routing
// -----------------------------------------------------------------------------

void Sequencer::setTarget(const std::string& targetName) {
    m_targetName = targetName;
    m_cachedTarget = nullptr;  // Will be resolved on next use
    resolveTargets();
}

void Sequencer::clearTarget() {
    // Send note-off if there's a playing note
    if (m_noteIsPlaying && m_cachedTarget) {
        m_cachedTarget->midiNoteOff(m_lastPlayedNote, 0.0f, static_cast<uint8_t>(static_cast<int>(midiChannel)));
    }
    m_targetName.clear();
    m_cachedTarget = nullptr;
}

void Sequencer::setMidiOutput(const std::string& midiOutName) {
    m_midiOutName = midiOutName;
    m_cachedMidiOut = nullptr;  // Will be resolved on next use
    resolveTargets();
}

void Sequencer::clearMidiOutput() {
    // Send note-off if there's a playing note
    if (m_noteIsPlaying && m_cachedMidiOut) {
        m_cachedMidiOut->sendNoteOff(static_cast<uint8_t>(static_cast<int>(midiChannel)), m_lastPlayedNote);
    }
    m_midiOutName.clear();
    m_cachedMidiOut = nullptr;
}

void Sequencer::resolveTargets() {
    if (!m_chain) return;

    // Resolve MidiReceiver target
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

    // Resolve MidiSender target (e.g., MidiOut)
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

    // Try to resolve targets if not yet resolved
    if ((!m_cachedTarget && !m_targetName.empty()) ||
        (!m_cachedMidiOut && !m_midiOutName.empty())) {
        resolveTargets();
    }

    // Send to internal MidiReceiver target
    if (m_cachedTarget) {
        m_cachedTarget->midiNoteOn(note, velocity, channel);
    }

    // Send to external MidiSender (e.g., MidiOut)
    if (m_cachedMidiOut) {
        m_cachedMidiOut->sendNoteOn(channel, note, velocity);
    }
}

void Sequencer::sendNoteOff(uint8_t note) {
    uint8_t channel = static_cast<uint8_t>(static_cast<int>(midiChannel));

    // Send to internal MidiReceiver target
    if (m_cachedTarget) {
        m_cachedTarget->midiNoteOff(note, 0.0f, channel);
    }

    // Send to external MidiSender (e.g., MidiOut)
    if (m_cachedMidiOut) {
        m_cachedMidiOut->sendNoteOff(channel, note);
    }
}

InspectData Sequencer::inspect() const {
    auto data = Operator::inspect();
    data.set("current_step", static_cast<float>(currentStep()));
    data.set("current_velocity", currentVelocity());
    data.set("current_note", static_cast<float>(currentNote()));
    int activeSteps = 0;
    for (int i = 0; i < static_cast<int>(steps); ++i) {
        if (m_pattern[i]) activeSteps++;
    }
    data.set("active_steps", static_cast<float>(activeSteps));
    return data;
}

} // namespace vivid::audio
