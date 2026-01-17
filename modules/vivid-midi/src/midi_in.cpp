#include <vivid/midi/midi_in.h>
#include <vivid/audio/midi_receiver.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <RtMidi.h>

#include <iostream>
#include <algorithm>

namespace vivid::midi {

REGISTER_OPERATOR(MidiIn, "MIDI", "Receive MIDI input from controllers and keyboards", false);

// -----------------------------------------------------------------------------
// Implementation (pimpl)
// -----------------------------------------------------------------------------

class MidiIn::Impl {
public:
    Impl() {
        try {
            m_midiIn = std::make_unique<RtMidiIn>();
        } catch (const RtMidiError& error) {
            std::cerr << "MidiIn: " << error.getMessage() << std::endl;
        }
    }

    ~Impl() {
        if (m_midiIn && m_midiIn->isPortOpen()) {
            m_midiIn->closePort();
        }
    }

    std::unique_ptr<RtMidiIn> m_midiIn;
    std::string m_portName;
};

// -----------------------------------------------------------------------------
// MidiIn
// -----------------------------------------------------------------------------

MidiIn::MidiIn() : m_impl(std::make_unique<Impl>()) {
    registerParam(channel);
    m_ccValues.fill(0.0f);
}

MidiIn::~MidiIn() = default;

std::vector<std::string> MidiIn::listPorts() {
    std::vector<std::string> ports;
    try {
        RtMidiIn midiIn;
        unsigned int count = midiIn.getPortCount();
        for (unsigned int i = 0; i < count; ++i) {
            ports.push_back(midiIn.getPortName(i));
        }
    } catch (const RtMidiError& error) {
        std::cerr << "MidiIn::listPorts: " << error.getMessage() << std::endl;
    }
    return ports;
}

void MidiIn::openPort(unsigned int portIndex) {
    if (!m_impl->m_midiIn) return;

    try {
        if (m_impl->m_midiIn->isPortOpen()) {
            m_impl->m_midiIn->closePort();
        }

        if (portIndex < m_impl->m_midiIn->getPortCount()) {
            m_impl->m_midiIn->openPort(portIndex);
            m_impl->m_portName = m_impl->m_midiIn->getPortName(portIndex);
            // Don't ignore sysex, timing, or active sensing
            m_impl->m_midiIn->ignoreTypes(false, false, false);
            std::cout << "MidiIn: Opened port " << m_impl->m_portName << std::endl;
        }
    } catch (const RtMidiError& error) {
        std::cerr << "MidiIn::openPort: " << error.getMessage() << std::endl;
    }
}

void MidiIn::openPortByName(const std::string& name) {
    if (!m_impl->m_midiIn) return;

    try {
        unsigned int count = m_impl->m_midiIn->getPortCount();
        for (unsigned int i = 0; i < count; ++i) {
            std::string portName = m_impl->m_midiIn->getPortName(i);
            // Case-insensitive partial match
            std::string lowerPort = portName;
            std::string lowerName = name;
            std::transform(lowerPort.begin(), lowerPort.end(), lowerPort.begin(), ::tolower);
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

            if (lowerPort.find(lowerName) != std::string::npos) {
                openPort(i);
                return;
            }
        }
        std::cerr << "MidiIn: No port matching '" << name << "' found" << std::endl;
    } catch (const RtMidiError& error) {
        std::cerr << "MidiIn::openPortByName: " << error.getMessage() << std::endl;
    }
}

void MidiIn::closePort() {
    if (m_impl->m_midiIn && m_impl->m_midiIn->isPortOpen()) {
        m_impl->m_midiIn->closePort();
        m_impl->m_portName.clear();
    }
}

bool MidiIn::isOpen() const {
    return m_impl->m_midiIn && m_impl->m_midiIn->isPortOpen();
}

const std::string& MidiIn::portName() const {
    return m_impl->m_portName;
}

bool MidiIn::noteOn(uint8_t noteNumber) const {
    if (noteNumber >= 128) return false;
    return m_noteOnThisFrame[noteNumber];
}

bool MidiIn::ccReceived(uint8_t ccNumber) const {
    if (ccNumber >= 128) return false;
    return m_ccReceivedThisFrame[ccNumber];
}

float MidiIn::cc(uint8_t ccNumber) const {
    if (ccNumber >= 128) return 0.0f;
    return m_ccValues[ccNumber];
}

void MidiIn::onNoteOn(std::function<void(uint8_t, float, uint8_t)> cb) {
    m_noteOnCallback = std::move(cb);
}

void MidiIn::onNoteOff(std::function<void(uint8_t, uint8_t)> cb) {
    m_noteOffCallback = std::move(cb);
}

void MidiIn::onCC(std::function<void(uint8_t, float, uint8_t)> cb) {
    m_ccCallback = std::move(cb);
}

void MidiIn::init(Context& ctx) {
    // Cache chain pointer for resolving targets
    m_chain = &ctx.chain();
}

void MidiIn::process(Context& /*ctx*/) {
    clearFrameState();

    if (!m_impl->m_midiIn || !m_impl->m_midiIn->isPortOpen()) {
        return;
    }

    // Poll for messages
    std::vector<unsigned char> message;
    while (true) {
        m_impl->m_midiIn->getMessage(&message);
        if (message.empty()) break;
        processMessage(message);
    }
}

void MidiIn::cleanup() {
    closePort();
    m_chain = nullptr;
    m_cachedTarget = nullptr;
}

// -----------------------------------------------------------------------------
// MIDI Routing
// -----------------------------------------------------------------------------

void MidiIn::setTarget(const std::string& targetName) {
    m_targetName = targetName;
    m_cachedTarget = nullptr;  // Will be resolved on first use
}

void MidiIn::clearTarget() {
    m_targetName.clear();
    m_cachedTarget = nullptr;
}

void MidiIn::setClockTarget(const std::string& targetName) {
    m_clockTargetName = targetName;
}

void MidiIn::clearClockTarget() {
    m_clockTargetName.clear();
}

void MidiIn::mapCC(uint8_t cc, const std::string& targetOp,
                   const std::string& paramName,
                   float minVal, float maxVal) {
    // Remove existing mapping for this CC
    unmapCC(cc);

    CCMapping mapping;
    mapping.cc = cc;
    mapping.targetOp = targetOp;
    mapping.paramName = paramName;
    mapping.minVal = minVal;
    mapping.maxVal = maxVal;
    m_ccMappings.push_back(mapping);
}

void MidiIn::unmapCC(uint8_t cc) {
    m_ccMappings.erase(
        std::remove_if(m_ccMappings.begin(), m_ccMappings.end(),
                       [cc](const CCMapping& m) { return m.cc == cc; }),
        m_ccMappings.end());
}

void MidiIn::clearCCMappings() {
    m_ccMappings.clear();
}

void MidiIn::routeToTarget(const MidiEvent& event) {
    if (m_targetName.empty() || !m_chain) return;

    // Resolve target if not cached
    if (!m_cachedTarget) {
        Operator* op = m_chain->getByName(m_targetName);
        if (op) {
            m_cachedTarget = dynamic_cast<audio::MidiReceiver*>(op);
            if (!m_cachedTarget) {
                std::cerr << "MidiIn: Target '" << m_targetName
                          << "' does not implement MidiReceiver" << std::endl;
            }
        } else {
            std::cerr << "MidiIn: Target '" << m_targetName << "' not found" << std::endl;
        }
    }

    if (!m_cachedTarget) return;

    // Route event to target
    switch (event.type) {
        case MidiEventType::NoteOn:
            m_cachedTarget->midiNoteOn(event.note, velocityToFloat(event.velocity), event.channel);
            break;

        case MidiEventType::NoteOff:
            m_cachedTarget->midiNoteOff(event.note, velocityToFloat(event.velocity), event.channel);
            break;

        case MidiEventType::PitchBend:
            m_cachedTarget->midiPitchBend(pitchBendToFloat(event.pitchBend), event.channel);
            break;

        case MidiEventType::ControlChange:
            // Check for special CCs
            if (event.cc == CC::AllNotesOff) {
                m_cachedTarget->midiAllNotesOff();
            } else if (event.cc == CC::AllSoundOff) {
                m_cachedTarget->midiPanic();
            } else {
                // Forward CC to receiver
                m_cachedTarget->midiCC(event.cc, ccToFloat(event.value), event.channel);
            }
            break;

        default:
            break;
    }
}

void MidiIn::applyCCMapping(uint8_t cc, float value) {
    if (!m_chain) return;

    for (const auto& mapping : m_ccMappings) {
        if (mapping.cc == cc) {
            Operator* target = m_chain->getByName(mapping.targetOp);
            if (target) {
                // Scale value from 0-1 to minVal-maxVal
                float scaled = mapping.minVal + (mapping.maxVal - mapping.minVal) * value;
                float paramVal[4] = {scaled, 0.0f, 0.0f, 0.0f};
                target->setParam(mapping.paramName, paramVal);
            }
        }
    }
}

void MidiIn::clearFrameState() {
    m_frameEvents.clear();
    m_hasNoteOn = false;
    m_hasNoteOff = false;
    m_hasCC = false;
    m_hasPitchBend = false;
    m_noteOnThisFrame.fill(false);
    m_ccReceivedThisFrame.fill(false);
}

void MidiIn::processMessage(const std::vector<unsigned char>& message) {
    if (message.empty()) return;

    uint8_t status = message[0];
    uint8_t msgChannel = status & 0x0F;
    uint8_t msgType = status & 0xF0;

    // Channel filter (0 = omni)
    int filterChannel = static_cast<int>(channel);
    if (filterChannel > 0 && msgChannel != (filterChannel - 1)) {
        return;
    }

    MidiEvent event;
    event.channel = msgChannel;

    switch (msgType) {
        case 0x90:  // Note On
            if (message.size() >= 3) {
                event.note = message[1];
                event.velocity = message[2];

                if (event.velocity > 0) {
                    event.type = MidiEventType::NoteOn;
                    m_hasNoteOn = true;
                    m_noteOnThisFrame[event.note] = true;
                    m_lastNote = event.note;
                    m_lastVelocity = velocityToFloat(event.velocity);

                    if (m_noteOnCallback) {
                        m_noteOnCallback(event.note, m_lastVelocity, event.channel);
                    }

                    // Auto-route to target synth
                    routeToTarget(event);
                } else {
                    // Velocity 0 = note off
                    event.type = MidiEventType::NoteOff;
                    m_hasNoteOff = true;

                    if (m_noteOffCallback) {
                        m_noteOffCallback(event.note, event.channel);
                    }

                    // Auto-route to target synth
                    routeToTarget(event);
                }
                m_frameEvents.push_back(event);
            }
            break;

        case 0x80:  // Note Off
            if (message.size() >= 3) {
                event.type = MidiEventType::NoteOff;
                event.note = message[1];
                event.velocity = message[2];
                m_hasNoteOff = true;

                if (m_noteOffCallback) {
                    m_noteOffCallback(event.note, event.channel);
                }

                // Auto-route to target synth
                routeToTarget(event);

                m_frameEvents.push_back(event);
            }
            break;

        case 0xB0:  // Control Change
            if (message.size() >= 3) {
                event.type = MidiEventType::ControlChange;
                event.cc = message[1];
                event.value = message[2];
                m_hasCC = true;
                m_ccReceivedThisFrame[event.cc] = true;
                m_ccValues[event.cc] = ccToFloat(event.value);

                if (m_ccCallback) {
                    m_ccCallback(event.cc, m_ccValues[event.cc], event.channel);
                }

                // Apply CC mappings and route to target
                applyCCMapping(event.cc, m_ccValues[event.cc]);
                routeToTarget(event);

                m_frameEvents.push_back(event);
            }
            break;

        case 0xC0:  // Program Change
            if (message.size() >= 2) {
                event.type = MidiEventType::ProgramChange;
                event.value = message[1];
                m_frameEvents.push_back(event);
            }
            break;

        case 0xE0:  // Pitch Bend
            if (message.size() >= 3) {
                event.type = MidiEventType::PitchBend;
                // Combine LSB and MSB into 14-bit value, center at 0
                int bend = (message[2] << 7) | message[1];
                event.pitchBend = static_cast<int16_t>(bend - 8192);
                m_hasPitchBend = true;
                m_pitchBendValue = pitchBendToFloat(event.pitchBend);

                // Auto-route to target synth
                routeToTarget(event);

                m_frameEvents.push_back(event);
            }
            break;

        case 0xD0:  // Channel Aftertouch
            if (message.size() >= 2) {
                event.type = MidiEventType::Aftertouch;
                event.value = message[1];
                m_frameEvents.push_back(event);
            }
            break;

        case 0xA0:  // Polyphonic Key Pressure
            if (message.size() >= 3) {
                event.type = MidiEventType::PolyPressure;
                event.note = message[1];
                event.value = message[2];
                m_frameEvents.push_back(event);
            }
            break;

        default:
            // System messages (clock, etc.)
            if (status == 0xF8) {
                event.type = MidiEventType::Clock;
                m_frameEvents.push_back(event);
            } else if (status == 0xFA) {
                event.type = MidiEventType::Start;
                m_frameEvents.push_back(event);
            } else if (status == 0xFB) {
                event.type = MidiEventType::Continue;
                m_frameEvents.push_back(event);
            } else if (status == 0xFC) {
                event.type = MidiEventType::Stop;
                m_frameEvents.push_back(event);
            }
            break;
    }
}

bool MidiIn::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    float w = maxX - minX;
    float h = maxY - minY;
    float cx = minX + w * 0.5f;
    float cy = minY + h * 0.5f;
    float r = std::min(w, h) * 0.35f;

    // Background circle
    bool open = isOpen();
    uint32_t bgColor = open ? VIZ_COL32(60, 30, 60, 255) : VIZ_COL32(60, 30, 30, 255);
    dl->AddCircleFilled(VizVec2(cx, cy), r, bgColor);
    dl->AddCircle(VizVec2(cx, cy), r, VIZ_COL32(100, 100, 100, 255), 32, 2.0f);

    // MIDI IN indicator
    uint32_t textColor = m_hasNoteOn ? VIZ_COL32(255, 100, 255, 255) : VIZ_COL32(180, 180, 180, 255);

    const char* label = "IN";
    VizTextSize textSize = dl->CalcTextSize(label);
    dl->AddText(VizVec2(cx - textSize.x * 0.5f, cy - textSize.y * 0.5f - r * 0.15f), textColor, label);

    // MIDI connector icon (5-pin DIN shape)
    float iconY = cy + r * 0.15f;
    float iconR = r * 0.25f;
    dl->AddCircle(VizVec2(cx, iconY), iconR, open ? VIZ_COL32(200, 100, 200, 255) : VIZ_COL32(150, 150, 150, 255), 16, 2.0f);

    // 5 small dots for pins
    for (int i = 0; i < 5; ++i) {
        float angle = (float)(i - 2) * 0.5f;  // Spread around center
        float px = cx + std::sin(angle) * iconR * 0.6f;
        float py = iconY + std::cos(angle) * iconR * 0.4f;
        dl->AddCircleFilled(VizVec2(px, py), 1.5f, open ? VIZ_COL32(200, 100, 200, 255) : VIZ_COL32(150, 150, 150, 255));
    }

    // Activity indicator dot
    if (m_hasNoteOn) {
        float dotR = r * 0.15f;
        dl->AddCircleFilled(VizVec2(cx + r * 0.6f, cy - r * 0.6f), dotR, VIZ_COL32(255, 100, 255, 255));
    }

    // Show last note if available
    if (m_hasNoteOn) {
        char noteStr[16];
        snprintf(noteStr, sizeof(noteStr), "%d", m_lastNote);
        VizTextSize noteSize = dl->CalcTextSize(noteStr);
        dl->AddText(VizVec2(cx - noteSize.x * 0.5f, maxY - noteSize.y - 2), VIZ_COL32(255, 100, 255, 200), noteStr);
    }

    return true;
}

} // namespace vivid::midi
