#include <vivid/midi/midi_out.h>
#include <vivid/context.h>
#include <RtMidi.h>
#include <iostream>
#include <algorithm>

namespace vivid::midi {

// -----------------------------------------------------------------------------
// Implementation (pimpl)
// -----------------------------------------------------------------------------

class MidiOut::Impl {
public:
    Impl() {
        try {
            m_midiOut = std::make_unique<RtMidiOut>();
        } catch (RtMidiError& error) {
            std::cerr << "MidiOut: " << error.getMessage() << std::endl;
        }
    }

    ~Impl() {
        if (m_midiOut && m_midiOut->isPortOpen()) {
            m_midiOut->closePort();
        }
    }

    std::unique_ptr<RtMidiOut> m_midiOut;
    std::string m_portName;
};

// -----------------------------------------------------------------------------
// MidiOut
// -----------------------------------------------------------------------------

MidiOut::MidiOut() : m_impl(std::make_unique<Impl>()) {
}

MidiOut::~MidiOut() = default;

std::vector<std::string> MidiOut::listPorts() {
    std::vector<std::string> ports;
    try {
        RtMidiOut midiOut;
        unsigned int count = midiOut.getPortCount();
        for (unsigned int i = 0; i < count; ++i) {
            ports.push_back(midiOut.getPortName(i));
        }
    } catch (RtMidiError& error) {
        std::cerr << "MidiOut::listPorts: " << error.getMessage() << std::endl;
    }
    return ports;
}

void MidiOut::openPort(unsigned int portIndex) {
    if (!m_impl->m_midiOut) return;

    try {
        if (m_impl->m_midiOut->isPortOpen()) {
            m_impl->m_midiOut->closePort();
        }

        if (portIndex < m_impl->m_midiOut->getPortCount()) {
            m_impl->m_midiOut->openPort(portIndex);
            m_impl->m_portName = m_impl->m_midiOut->getPortName(portIndex);
            std::cout << "MidiOut: Opened port " << m_impl->m_portName << std::endl;
        }
    } catch (RtMidiError& error) {
        std::cerr << "MidiOut::openPort: " << error.getMessage() << std::endl;
    }
}

void MidiOut::openPortByName(const std::string& name) {
    if (!m_impl->m_midiOut) return;

    try {
        unsigned int count = m_impl->m_midiOut->getPortCount();
        for (unsigned int i = 0; i < count; ++i) {
            std::string portName = m_impl->m_midiOut->getPortName(i);
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
        std::cerr << "MidiOut: No port matching '" << name << "' found" << std::endl;
    } catch (RtMidiError& error) {
        std::cerr << "MidiOut::openPortByName: " << error.getMessage() << std::endl;
    }
}

void MidiOut::closePort() {
    if (m_impl->m_midiOut && m_impl->m_midiOut->isPortOpen()) {
        m_impl->m_midiOut->closePort();
        m_impl->m_portName.clear();
    }
}

bool MidiOut::isOpen() const {
    return m_impl->m_midiOut && m_impl->m_midiOut->isPortOpen();
}

std::string MidiOut::portName() const {
    return m_impl->m_portName;
}

void MidiOut::noteOn(uint8_t channel, uint8_t note, float velocity) {
    std::vector<unsigned char> message = {
        static_cast<unsigned char>(0x90 | (channel & 0x0F)),
        static_cast<unsigned char>(note & 0x7F),
        static_cast<unsigned char>(floatToVelocity(velocity))
    };
    sendRaw(message);
}

void MidiOut::noteOff(uint8_t channel, uint8_t note) {
    std::vector<unsigned char> message = {
        static_cast<unsigned char>(0x80 | (channel & 0x0F)),
        static_cast<unsigned char>(note & 0x7F),
        static_cast<unsigned char>(0)
    };
    sendRaw(message);
}

void MidiOut::sendCC(uint8_t channel, uint8_t cc, float value) {
    std::vector<unsigned char> message = {
        static_cast<unsigned char>(0xB0 | (channel & 0x0F)),
        static_cast<unsigned char>(cc & 0x7F),
        static_cast<unsigned char>(floatToCC(value))
    };
    sendRaw(message);
}

void MidiOut::programChange(uint8_t channel, uint8_t program) {
    std::vector<unsigned char> message = {
        static_cast<unsigned char>(0xC0 | (channel & 0x0F)),
        static_cast<unsigned char>(program & 0x7F)
    };
    sendRaw(message);
}

void MidiOut::sendPitchBend(uint8_t channel, float bend) {
    int16_t bendValue = floatToPitchBend(bend);
    int midiValue = bendValue + 8192;  // Convert to 0-16383 range
    uint8_t lsb = midiValue & 0x7F;
    uint8_t msb = (midiValue >> 7) & 0x7F;

    std::vector<unsigned char> message = {
        static_cast<unsigned char>(0xE0 | (channel & 0x0F)),
        lsb,
        msb
    };
    sendRaw(message);
}

void MidiOut::send(const MidiEvent& event) {
    switch (event.type) {
        case MidiEventType::NoteOn:
            noteOn(event.channel, event.note, velocityToFloat(event.velocity));
            break;
        case MidiEventType::NoteOff:
            noteOff(event.channel, event.note);
            break;
        case MidiEventType::ControlChange:
            sendCC(event.channel, event.cc, ccToFloat(event.value));
            break;
        case MidiEventType::ProgramChange:
            programChange(event.channel, event.value);
            break;
        case MidiEventType::PitchBend:
            sendPitchBend(event.channel, pitchBendToFloat(event.pitchBend));
            break;
        default:
            break;
    }
}

void MidiOut::allNotesOff(uint8_t channel) {
    sendCC(channel, CC::AllNotesOff, 0.0f);
}

void MidiOut::panic() {
    for (uint8_t ch = 0; ch < 16; ++ch) {
        allNotesOff(ch);
    }
}

void MidiOut::init(Context& /*ctx*/) {
    // Already initialized in constructor
}

void MidiOut::process(Context& /*ctx*/) {
    // Nothing to do - output is immediate
}

void MidiOut::cleanup() {
    panic();  // Turn off all notes
    closePort();
}

void MidiOut::sendRaw(const std::vector<unsigned char>& message) {
    if (!m_impl->m_midiOut || !m_impl->m_midiOut->isPortOpen()) {
        return;
    }

    try {
        m_impl->m_midiOut->sendMessage(&message);
    } catch (RtMidiError& error) {
        std::cerr << "MidiOut::sendRaw: " << error.getMessage() << std::endl;
    }
}

} // namespace vivid::midi
