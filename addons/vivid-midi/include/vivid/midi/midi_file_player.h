#pragma once

/**
 * @file midi_file_player.h
 * @brief Standard MIDI File playback operator
 *
 * Loads and plays .mid files with tempo synchronization.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/param_registry.h>
#include <vivid/midi/midi_event.h>
#include <string>
#include <vector>
#include <memory>

namespace vivid::audio { class Clock; }

namespace vivid::midi {

/**
 * @brief Standard MIDI File playback operator
 *
 * Plays back .mid files (Type 0 and Type 1) with optional tempo
 * synchronization to a Clock operator.
 *
 * @par Example
 * @code
 * auto& player = chain.add<MidiFilePlayer>("player");
 * player.load("song.mid");
 * player.syncToClock(&clock);  // Use Clock's BPM
 * player.loop = true;
 * player.play();
 *
 * // In update():
 * for (const auto& e : player.events()) {
 *     if (e.type == MidiEventType::NoteOn) {
 *         synth.noteOn(midiToFreq(e.note));
 *     }
 * }
 * @endcode
 */
class MidiFilePlayer : public Operator, public ParamRegistry {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<bool> loop{"loop", false};        ///< Loop playback
    Param<int> track{"track", -1, -1, 64};  ///< Track filter (-1 = all tracks)

    /// @}
    // -------------------------------------------------------------------------

    MidiFilePlayer();
    ~MidiFilePlayer() override;

    // -------------------------------------------------------------------------
    /// @name File Loading
    /// @{

    /// @brief Load a Standard MIDI File
    /// @param path Path to .mid file
    /// @return true if loaded successfully
    bool load(const std::string& path);

    /// @brief Unload the current file
    void unload();

    /// @brief Check if a file is loaded
    bool isLoaded() const;

    /// @brief Get the number of tracks in the file
    int trackCount() const;

    /// @brief Get ticks per quarter note (PPQ) from file header
    int ticksPerBeat() const;

    /// @brief Get total duration in seconds (at current tempo)
    double durationSeconds() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Tempo Synchronization
    /// @{

    /// @brief Sync playback tempo to a Clock operator
    /// @param clock Pointer to Clock (nullptr to use file tempo)
    void syncToClock(vivid::audio::Clock* clock);

    /// @brief Use the tempo embedded in the MIDI file
    void useFileTempo();

    /// @brief Get current playback tempo in BPM
    double tempo() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /// @brief Start playback
    void play();

    /// @brief Pause playback (maintains position)
    void pause();

    /// @brief Stop playback and reset to beginning
    void stop();

    /// @brief Seek to a position in seconds
    void seek(double seconds);

    /// @brief Check if currently playing
    bool isPlaying() const;

    /// @brief Get current playback position in seconds
    double position() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Event Access (poll each frame)
    /// @{

    /// @brief Get MIDI events that occurred this frame
    const std::vector<MidiEvent>& events() const { return m_frameEvents; }

    /// @brief Check if any note-on occurred this frame
    bool noteOn() const { return m_hasNoteOn; }

    /// @brief Get most recent note number (if noteOn() is true)
    uint8_t note() const { return m_lastNote; }

    /// @brief Get most recent velocity (0.0-1.0)
    float velocity() const { return m_lastVelocity; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "MidiFilePlayer"; }
    OutputKind outputKind() const override { return OutputKind::Value; }
    bool drawVisualization(ImDrawList* dl, float minX, float minY, float maxX, float maxY) override;

    std::vector<ParamDecl> params() override { return registeredParams(); }
    bool getParam(const std::string& name, float out[4]) override {
        return getRegisteredParam(name, out);
    }
    bool setParam(const std::string& name, const float value[4]) override {
        return setRegisteredParam(name, value);
    }

    /// @}

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    // Per-frame event buffer
    std::vector<MidiEvent> m_frameEvents;

    // Cached state
    bool m_hasNoteOn = false;
    uint8_t m_lastNote = 60;
    float m_lastVelocity = 0.0f;

    // Clock sync
    vivid::audio::Clock* m_clock = nullptr;

    void clearFrameState();
};

} // namespace vivid::midi
