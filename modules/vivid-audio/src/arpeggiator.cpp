#include <vivid/audio/arpeggiator.h>
#include <vivid/audio/clock.h>
#include <vivid/audio/midi_sender.h>
#include <vivid/operator_registry.h>
#include <vivid/audio_graph.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <algorithm>
#include <iostream>
#include <random>

namespace vivid::audio {

REGISTER_OPERATOR(Arpeggiator, "Audio Sequencing", "MIDI arpeggiator for creating patterns from held notes", false);

// Random number generator for Random mode
static std::mt19937& getRng() {
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

Arpeggiator::Arpeggiator() {
    registerParam(octaves);
    registerParam(gate);
    registerParam(midiChannel);
}

void Arpeggiator::init(Context& ctx) {
    if (!beginInit()) return;

    // Allocate minimal output buffer
    allocateOutput(256, 2, AUDIO_SAMPLE_RATE);

    // Cache chain pointer for MIDI routing
    m_chain = &ctx.chain();

    // Reset state
    m_currentStep.store(0, std::memory_order_relaxed);
    m_currentNote.store(0, std::memory_order_relaxed);
    m_triggeredFlag.store(false, std::memory_order_relaxed);
    m_pendingTrigger = false;
    m_noteIsPlaying = false;
    m_goingUp = true;

    // Sync with trigger source
    Operator* trigSource = triggerSource();
    if (trigSource) {
        if (auto* clock = dynamic_cast<Clock*>(trigSource)) {
            m_lastTriggerCount = clock->triggerCount();
        }
    } else {
        m_lastTriggerCount = 0;
    }

    // Resolve MIDI targets
    resolveTargets();
}

void Arpeggiator::process(Context& /*ctx*/) {
    // Main thread process is a no-op
    // All timing happens in generateBlock()
}

void Arpeggiator::generateBlock(uint32_t frameCount) {
    // Clear trigger flag at start of block
    m_triggeredFlag.store(false, std::memory_order_release);

    bool triggeredThisBlock = false;

    // Check trigger source (typically Clock)
    Operator* trigSource = triggerSource();
    if (trigSource) {
        if (auto* clock = dynamic_cast<Clock*>(trigSource)) {
            uint64_t currentCount = clock->triggerCount();
            if (currentCount > m_lastTriggerCount) {
                // Process each trigger
                uint64_t triggers = currentCount - m_lastTriggerCount;
                for (uint64_t i = 0; i < triggers; ++i) {
                    if (m_heldNoteCount.load(std::memory_order_relaxed) > 0) {
                        triggeredThisBlock = true;
                        advanceArpeggio();
                    }
                }
                m_lastTriggerCount = currentCount;
            }
        }
    }

    // Handle pending trigger from onTrigger()
    if (m_pendingTrigger) {
        m_pendingTrigger = false;
        if (m_heldNoteCount.load(std::memory_order_relaxed) > 0) {
            triggeredThisBlock = true;
            advanceArpeggio();
        }
    }

    if (triggeredThisBlock) {
        m_triggeredFlag.store(true, std::memory_order_release);
        playCurrentNote();
    }

    // Resize output buffer if needed
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }
}

void Arpeggiator::onTrigger() {
    // If using Clock, we poll triggerCount in generateBlock()
    Operator* src = triggerSource();
    if (src && dynamic_cast<Clock*>(src)) {
        return;
    }
    m_pendingTrigger = true;
}

void Arpeggiator::cleanup() {
    // Stop any playing note
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
// MidiReceiver Interface
// -----------------------------------------------------------------------------

void Arpeggiator::midiNoteOn(uint8_t note, float velocity, uint8_t /*channel*/) {
    int count = m_heldNoteCount.load(std::memory_order_relaxed);
    if (count >= MAX_HELD_NOTES) {
        return;  // Buffer full
    }

    // Check if note is already held
    for (int i = 0; i < count; ++i) {
        if (m_heldNotes[i].note == note) {
            // Update velocity
            m_heldNotes[i].velocity = velocity;
            return;
        }
    }

    // Add new note
    m_heldNotes[count].note = note;
    m_heldNotes[count].velocity = velocity;
    m_heldNotes[count].order = m_noteOrderCounter++;
    m_heldNoteCount.store(count + 1, std::memory_order_release);

    // Sort for pitch-based modes
    sortHeldNotes();
}

void Arpeggiator::midiNoteOff(uint8_t note, float /*velocity*/, uint8_t /*channel*/) {
    int count = m_heldNoteCount.load(std::memory_order_relaxed);

    // Find and remove the note
    for (int i = 0; i < count; ++i) {
        if (m_heldNotes[i].note == note) {
            // Shift remaining notes down
            for (int j = i; j < count - 1; ++j) {
                m_heldNotes[j] = m_heldNotes[j + 1];
            }
            m_heldNoteCount.store(count - 1, std::memory_order_release);

            // If no notes left, stop any playing note
            if (count - 1 == 0 && m_noteIsPlaying) {
                stopCurrentNote();
            }
            return;
        }
    }
}

void Arpeggiator::midiAllNotesOff() {
    // Stop playing note first
    if (m_noteIsPlaying) {
        stopCurrentNote();
    }

    // Clear held notes
    m_heldNoteCount.store(0, std::memory_order_release);
    m_currentStep.store(0, std::memory_order_relaxed);
    m_goingUp = true;
}

void Arpeggiator::midiPanic() {
    midiAllNotesOff();
}

// -----------------------------------------------------------------------------
// MIDI Routing
// -----------------------------------------------------------------------------

void Arpeggiator::setTarget(const std::string& targetName) {
    m_targetName = targetName;
    m_cachedTarget = nullptr;
    resolveTargets();
}

void Arpeggiator::clearTarget() {
    if (m_noteIsPlaying && m_cachedTarget) {
        m_cachedTarget->midiNoteOff(m_lastPlayedNote, 0.0f,
            static_cast<uint8_t>(static_cast<int>(midiChannel)));
    }
    m_targetName.clear();
    m_cachedTarget = nullptr;
}

void Arpeggiator::setMidiOutput(const std::string& midiOutName) {
    m_midiOutName = midiOutName;
    m_cachedMidiOut = nullptr;
    resolveTargets();
}

void Arpeggiator::clearMidiOutput() {
    if (m_noteIsPlaying && m_cachedMidiOut) {
        m_cachedMidiOut->sendNoteOff(static_cast<uint8_t>(static_cast<int>(midiChannel)),
            m_lastPlayedNote);
    }
    m_midiOutName.clear();
    m_cachedMidiOut = nullptr;
}

void Arpeggiator::resolveTargets() {
    if (!m_chain) return;

    if (!m_targetName.empty() && !m_cachedTarget) {
        Operator* op = m_chain->getByName(m_targetName);
        if (op) {
            m_cachedTarget = dynamic_cast<MidiReceiver*>(op);
            if (!m_cachedTarget) {
                std::cerr << "Arpeggiator: Target '" << m_targetName
                          << "' does not implement MidiReceiver" << std::endl;
            }
        }
    }

    if (!m_midiOutName.empty() && !m_cachedMidiOut) {
        Operator* op = m_chain->getByName(m_midiOutName);
        if (op) {
            m_cachedMidiOut = dynamic_cast<MidiSender*>(op);
            if (!m_cachedMidiOut) {
                std::cerr << "Arpeggiator: MidiOut '" << m_midiOutName
                          << "' does not implement MidiSender" << std::endl;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

void Arpeggiator::reset() {
    if (m_noteIsPlaying) {
        stopCurrentNote();
    }
    m_currentStep.store(0, std::memory_order_relaxed);
    m_goingUp = true;
    m_pendingTrigger = false;
    m_lastTriggerCount = 0;
}

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------

void Arpeggiator::advanceArpeggio() {
    int noteCount = m_heldNoteCount.load(std::memory_order_relaxed);
    if (noteCount == 0) return;

    // Calculate total notes including octaves
    int numOctaves = static_cast<int>(octaves);
    int totalNotes = noteCount * numOctaves;

    // Get next note index based on mode
    int nextIndex = getNextNoteIndex();

    // Store current step
    m_currentStep.store(nextIndex, std::memory_order_relaxed);
}

int Arpeggiator::getNextNoteIndex() {
    int noteCount = m_heldNoteCount.load(std::memory_order_relaxed);
    if (noteCount == 0) return 0;

    int numOctaves = static_cast<int>(octaves);
    int totalNotes = noteCount * numOctaves;
    int currentStep = m_currentStep.load(std::memory_order_relaxed);

    switch (m_mode) {
        case ArpMode::Up: {
            return (currentStep + 1) % totalNotes;
        }

        case ArpMode::Down: {
            currentStep--;
            if (currentStep < 0) currentStep = totalNotes - 1;
            return currentStep;
        }

        case ArpMode::UpDown: {
            if (totalNotes == 1) return 0;

            if (m_goingUp) {
                currentStep++;
                if (currentStep >= totalNotes - 1) {
                    currentStep = totalNotes - 1;
                    m_goingUp = false;
                }
            } else {
                currentStep--;
                if (currentStep <= 0) {
                    currentStep = 0;
                    m_goingUp = true;
                }
            }
            return currentStep;
        }

        case ArpMode::Random: {
            std::uniform_int_distribution<int> dist(0, totalNotes - 1);
            return dist(getRng());
        }

        case ArpMode::Order: {
            // Notes are already in order by press time
            return (currentStep + 1) % totalNotes;
        }

        default:
            return 0;
    }
}

void Arpeggiator::playCurrentNote() {
    int noteCount = m_heldNoteCount.load(std::memory_order_relaxed);
    if (noteCount == 0) return;

    // Stop previous note
    if (m_noteIsPlaying) {
        stopCurrentNote();
    }

    int step = m_currentStep.load(std::memory_order_relaxed);
    int numOctaves = static_cast<int>(octaves);

    // Calculate which base note and octave
    int baseNoteIndex = step % noteCount;
    int octaveOffset = step / noteCount;

    // Get note and velocity
    uint8_t baseNote = m_heldNotes[baseNoteIndex].note;
    float velocity = m_heldNotes[baseNoteIndex].velocity;

    // Apply octave offset
    uint8_t note = baseNote + static_cast<uint8_t>(octaveOffset * 12);

    // Clamp to valid MIDI range
    if (note > 127) note = 127;

    m_currentNote.store(note, std::memory_order_relaxed);
    sendNoteOn(note, velocity);
    m_lastPlayedNote = note;
    m_noteIsPlaying = true;
}

void Arpeggiator::stopCurrentNote() {
    if (m_noteIsPlaying) {
        sendNoteOff(m_lastPlayedNote);
        m_noteIsPlaying = false;
    }
}

void Arpeggiator::sendNoteOn(uint8_t note, float velocity) {
    uint8_t channel = static_cast<uint8_t>(static_cast<int>(midiChannel));

    // Resolve targets if needed
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

void Arpeggiator::sendNoteOff(uint8_t note) {
    uint8_t channel = static_cast<uint8_t>(static_cast<int>(midiChannel));

    if (m_cachedTarget) {
        m_cachedTarget->midiNoteOff(note, 0.0f, channel);
    }

    if (m_cachedMidiOut) {
        m_cachedMidiOut->sendNoteOff(channel, note);
    }
}

void Arpeggiator::sortHeldNotes() {
    int count = m_heldNoteCount.load(std::memory_order_relaxed);
    if (count <= 1) return;

    // For Up/Down modes, sort by pitch
    // For Order mode, sort by order field
    if (m_mode == ArpMode::Order) {
        std::sort(m_heldNotes.begin(), m_heldNotes.begin() + count,
            [](const HeldNote& a, const HeldNote& b) {
                return a.order < b.order;
            });
    } else {
        // Sort by pitch (lowest to highest)
        std::sort(m_heldNotes.begin(), m_heldNotes.begin() + count,
            [](const HeldNote& a, const HeldNote& b) {
                return a.note < b.note;
            });
    }
}

} // namespace vivid::audio
