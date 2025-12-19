#include <vivid/midi/midi_in.h>
#include <vivid/context.h>
#include <RtMidi.h>
#include <iostream>
#include <algorithm>

namespace vivid::midi {

// -----------------------------------------------------------------------------
// Implementation (pimpl)
// -----------------------------------------------------------------------------

class MidiIn::Impl {
public:
    Impl() {
        try {
            m_midiIn = std::make_unique<RtMidiIn>();
        } catch (RtMidiError& error) {
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
    } catch (RtMidiError& error) {
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
    } catch (RtMidiError& error) {
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
    } catch (RtMidiError& error) {
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

std::string MidiIn::portName() const {
    return m_impl->m_portName;
}

bool MidiIn::noteOn(uint8_t noteNumber) const {
    return m_noteOnThisFrame[noteNumber];
}

bool MidiIn::ccReceived(uint8_t ccNumber) const {
    return m_ccReceivedThisFrame[ccNumber];
}

float MidiIn::cc(uint8_t ccNumber) const {
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

void MidiIn::init(Context& /*ctx*/) {
    // Already initialized in constructor
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
                } else {
                    // Velocity 0 = note off
                    event.type = MidiEventType::NoteOff;
                    m_hasNoteOff = true;

                    if (m_noteOffCallback) {
                        m_noteOffCallback(event.note, event.channel);
                    }
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

} // namespace vivid::midi
