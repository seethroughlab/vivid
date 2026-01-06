#include <vivid/midi/midi_file_player.h>
#include <vivid/context.h>
#include <vivid/audio/clock.h>
#include <MidiFile.h>

#include <iostream>
#include <cmath>

namespace vivid::midi {

// -----------------------------------------------------------------------------
// Implementation (pimpl)
// -----------------------------------------------------------------------------

class MidiFilePlayer::Impl {
public:
    smf::MidiFile m_midiFile;
    bool m_loaded = false;
    bool m_playing = false;

    // Playback state
    double m_position = 0.0;         // Current position in seconds
    double m_fileTempo = 120.0;      // Tempo from file (or default)
    double m_duration = 0.0;         // Total duration in seconds
    int m_ticksPerBeat = 480;        // PPQ from file

    // Event tracking
    std::vector<size_t> m_trackPositions;  // Current event index per track
    double m_lastFrameTime = 0.0;

    void reset() {
        m_position = 0.0;
        m_trackPositions.clear();
        if (m_loaded) {
            m_trackPositions.resize(m_midiFile.getTrackCount(), 0);
        }
    }

    double getTempo() const {
        return m_fileTempo;
    }
};

// -----------------------------------------------------------------------------
// MidiFilePlayer
// -----------------------------------------------------------------------------

MidiFilePlayer::MidiFilePlayer() : m_impl(std::make_unique<Impl>()) {
    registerParam(loop);
    registerParam(track);
}

MidiFilePlayer::~MidiFilePlayer() = default;

bool MidiFilePlayer::load(const std::string& path) {
    unload();

    try {
        if (!m_impl->m_midiFile.read(path)) {
            std::cerr << "MidiFilePlayer: Failed to load " << path << std::endl;
            return false;
        }

        // Convert to absolute ticks and link note pairs
        m_impl->m_midiFile.doTimeAnalysis();
        m_impl->m_midiFile.linkNotePairs();

        m_impl->m_ticksPerBeat = m_impl->m_midiFile.getTicksPerQuarterNote();
        m_impl->m_duration = m_impl->m_midiFile.getFileDurationInSeconds();
        m_impl->m_loaded = true;

        // Initialize track positions
        m_impl->m_trackPositions.resize(m_impl->m_midiFile.getTrackCount(), 0);

        // Try to get tempo from file (first tempo event)
        for (int t = 0; t < m_impl->m_midiFile.getTrackCount(); ++t) {
            for (int e = 0; e < m_impl->m_midiFile[t].size(); ++e) {
                auto& event = m_impl->m_midiFile[t][e];
                if (event.isTempo()) {
                    m_impl->m_fileTempo = event.getTempoBPM();
                    break;
                }
            }
        }

        std::cout << "MidiFilePlayer: Loaded " << path
                  << " (" << trackCount() << " tracks, "
                  << m_impl->m_duration << "s, "
                  << m_impl->m_fileTempo << " BPM)" << std::endl;

        return true;
    } catch (const std::exception& e) {
        std::cerr << "MidiFilePlayer: Exception loading " << path << ": " << e.what() << std::endl;
        return false;
    }
}

void MidiFilePlayer::unload() {
    m_impl->m_midiFile.clear();
    m_impl->m_loaded = false;
    m_impl->m_playing = false;
    m_impl->reset();
}

bool MidiFilePlayer::isLoaded() const {
    return m_impl->m_loaded;
}

int MidiFilePlayer::trackCount() const {
    return m_impl->m_loaded ? m_impl->m_midiFile.getTrackCount() : 0;
}

int MidiFilePlayer::ticksPerBeat() const {
    return m_impl->m_ticksPerBeat;
}

double MidiFilePlayer::durationSeconds() const {
    return m_impl->m_duration;
}

void MidiFilePlayer::syncToClock(vivid::audio::Clock* clock) {
    m_clock = clock;
}

void MidiFilePlayer::useFileTempo() {
    m_clock = nullptr;
}

double MidiFilePlayer::tempo() const {
    if (m_clock) {
        return static_cast<double>(static_cast<float>(m_clock->bpm));
    }
    return m_impl->m_fileTempo;
}

void MidiFilePlayer::play() {
    m_impl->m_playing = true;
}

void MidiFilePlayer::pause() {
    m_impl->m_playing = false;
}

void MidiFilePlayer::stop() {
    m_impl->m_playing = false;
    m_impl->reset();
}

void MidiFilePlayer::seek(double seconds) {
    m_impl->m_position = std::clamp(seconds, 0.0, m_impl->m_duration);

    // Update track positions to match seek position
    for (int t = 0; t < m_impl->m_midiFile.getTrackCount(); ++t) {
        m_impl->m_trackPositions[t] = 0;
        for (int e = 0; e < m_impl->m_midiFile[t].size(); ++e) {
            if (m_impl->m_midiFile[t][e].seconds >= m_impl->m_position) {
                m_impl->m_trackPositions[t] = e;
                break;
            }
        }
    }
}

bool MidiFilePlayer::isPlaying() const {
    return m_impl->m_playing;
}

double MidiFilePlayer::position() const {
    return m_impl->m_position;
}

void MidiFilePlayer::init(Context& /*ctx*/) {
    // Already initialized in constructor
}

void MidiFilePlayer::process(Context& ctx) {
    clearFrameState();

    if (!m_impl->m_loaded || !m_impl->m_playing) {
        return;
    }

    // Calculate time delta (adjust for tempo if synced to clock)
    double dt = ctx.dt();

    // If synced to a clock, scale time by tempo ratio
    if (m_clock) {
        double clockBpm = static_cast<double>(static_cast<float>(m_clock->bpm));
        double tempoRatio = clockBpm / m_impl->m_fileTempo;
        dt *= tempoRatio;
    }

    double prevPosition = m_impl->m_position;
    double newPosition = prevPosition + dt;

    // Check for loop
    if (newPosition >= m_impl->m_duration) {
        if (static_cast<bool>(loop)) {
            newPosition = std::fmod(newPosition, m_impl->m_duration);
            m_impl->reset();
        } else {
            m_impl->m_playing = false;
            newPosition = m_impl->m_duration;
        }
    }

    m_impl->m_position = newPosition;

    // Collect events in time window [prevPosition, newPosition)
    int filterTrack = static_cast<int>(track);

    for (int t = 0; t < m_impl->m_midiFile.getTrackCount(); ++t) {
        // Skip if filtering to a specific track
        if (filterTrack >= 0 && t != filterTrack) {
            continue;
        }

        auto& trackEvents = m_impl->m_midiFile[t];
        size_t& pos = m_impl->m_trackPositions[t];

        while (pos < static_cast<size_t>(trackEvents.size())) {
            auto& event = trackEvents[pos];
            double eventTime = event.seconds;

            if (eventTime >= newPosition) {
                break;  // Event is in the future
            }

            if (eventTime >= prevPosition) {
                // Event is within this frame's time window
                MidiEvent midiEvent;
                midiEvent.channel = event.getChannel();
                midiEvent.timestamp = 0;  // Could calculate sample offset

                if (event.isNoteOn()) {
                    midiEvent.type = MidiEventType::NoteOn;
                    midiEvent.note = event.getKeyNumber();
                    midiEvent.velocity = event.getVelocity();

                    if (midiEvent.velocity > 0) {
                        m_hasNoteOn = true;
                        m_lastNote = midiEvent.note;
                        m_lastVelocity = velocityToFloat(midiEvent.velocity);
                    } else {
                        // Velocity 0 = note off
                        midiEvent.type = MidiEventType::NoteOff;
                    }
                    m_frameEvents.push_back(midiEvent);
                }
                else if (event.isNoteOff()) {
                    midiEvent.type = MidiEventType::NoteOff;
                    midiEvent.note = event.getKeyNumber();
                    midiEvent.velocity = event.getVelocity();
                    m_frameEvents.push_back(midiEvent);
                }
                else if (event.isController()) {
                    midiEvent.type = MidiEventType::ControlChange;
                    midiEvent.cc = event.getControllerNumber();
                    midiEvent.value = event.getControllerValue();
                    m_frameEvents.push_back(midiEvent);
                }
                else if (event.isPitchbend()) {
                    midiEvent.type = MidiEventType::PitchBend;
                    // Pitch bend is 14-bit, stored in two data bytes
                    int bend = (event[2] << 7) | event[1];
                    midiEvent.pitchBend = static_cast<int16_t>(bend - 8192);
                    m_frameEvents.push_back(midiEvent);
                }
            }

            ++pos;
        }
    }
}

void MidiFilePlayer::cleanup() {
    unload();
}

void MidiFilePlayer::clearFrameState() {
    m_frameEvents.clear();
    m_hasNoteOn = false;
}

bool MidiFilePlayer::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    float w = maxX - minX;
    float h = maxY - minY;
    float cx = minX + w * 0.5f;

    // Background
    bool loaded = isLoaded();
    bool playing = isPlaying();
    uint32_t bgColor = loaded ? (playing ? VIZ_COL32(40, 50, 40, 255) : VIZ_COL32(40, 40, 50, 255)) : VIZ_COL32(60, 30, 30, 255);
    dl->AddRectFilled(VizVec2(minX, minY), VizVec2(maxX, maxY), bgColor);

    // File label
    const char* label = "MIDI";
    VizTextSize textSize = dl->CalcTextSize(label);
    uint32_t textColor = loaded ? VIZ_COL32(150, 200, 150, 255) : VIZ_COL32(150, 150, 150, 255);
    dl->AddText(VizVec2(cx - textSize.x * 0.5f, minY + 4), textColor, label);

    // Progress bar
    if (loaded && m_impl->m_duration > 0) {
        float barY = minY + 20;
        float barHeight = 6;
        float progress = static_cast<float>(position() / m_impl->m_duration);

        // Bar background
        dl->AddRectFilled(VizVec2(minX + 4, barY), VizVec2(maxX - 4, barY + barHeight), VIZ_COL32(30, 30, 30, 255));

        // Progress fill
        float fillWidth = (w - 8) * progress;
        uint32_t fillColor = playing ? VIZ_COL32(100, 200, 100, 255) : VIZ_COL32(100, 150, 200, 255);
        dl->AddRectFilled(VizVec2(minX + 4, barY), VizVec2(minX + 4 + fillWidth, barY + barHeight), fillColor);
    }

    // Play/pause indicator
    float iconY = minY + h * 0.5f + 5;
    float iconSize = std::min(w, h) * 0.15f;
    uint32_t iconColor = loaded ? VIZ_COL32(200, 200, 200, 255) : VIZ_COL32(100, 100, 100, 255);

    if (playing) {
        // Pause icon (two vertical bars)
        dl->AddRectFilled(VizVec2(cx - iconSize * 0.6f, iconY - iconSize * 0.5f),
                          VizVec2(cx - iconSize * 0.2f, iconY + iconSize * 0.5f), iconColor);
        dl->AddRectFilled(VizVec2(cx + iconSize * 0.2f, iconY - iconSize * 0.5f),
                          VizVec2(cx + iconSize * 0.6f, iconY + iconSize * 0.5f), iconColor);
    } else {
        // Play icon (triangle)
        dl->AddTriangleFilled(
            VizVec2(cx - iconSize * 0.4f, iconY - iconSize * 0.5f),
            VizVec2(cx - iconSize * 0.4f, iconY + iconSize * 0.5f),
            VizVec2(cx + iconSize * 0.6f, iconY),
            iconColor);
    }

    // Note activity indicator
    if (m_hasNoteOn) {
        float dotR = std::min(w, h) * 0.05f;
        dl->AddCircleFilled(VizVec2(maxX - dotR - 4, minY + dotR + 4), dotR, VIZ_COL32(100, 255, 100, 255));
    }

    return true;
}

} // namespace vivid::midi
