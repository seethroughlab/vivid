#pragma once

/**
 * @file drum_kit.h
 * @brief MIDI-controlled drum kit with multiple drum synthesizers
 *
 * A complete drum kit that receives MIDI notes and triggers the appropriate
 * drum sounds. Uses General MIDI drum mapping by default.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/midi_receiver.h>
#include <vivid/audio/kick.h>
#include <vivid/audio/snare.h>
#include <vivid/audio/hihat.h>
#include <vivid/audio/clap.h>
#include <vivid/audio/tom.h>
#include <vivid/audio/cymbal.h>
#include <vivid/param.h>
#include <string>
#include <array>
#include <memory>
#include <unordered_map>

namespace vivid::audio {

/**
 * @brief Drum type identifiers
 */
enum class DrumType {
    Kick,
    Snare,
    ClosedHiHat,
    OpenHiHat,
    Clap,
    LowTom,
    MidTom,
    HighTom,
    Crash,
    Ride,
    Count  // Number of drum types
};

/**
 * @brief MIDI-controlled drum kit
 *
 * A complete drum kit that receives MIDI notes (via MidiReceiver interface)
 * and triggers the appropriate drum synthesizers. Perfect for use with
 * Sequencer for step-sequenced drum patterns.
 *
 * Uses General MIDI drum mapping by default:
 * | Note | Drum |
 * |------|------|
 * | 36 (C2) | Kick |
 * | 38 (D2) | Snare |
 * | 42 (F#2) | Closed Hi-Hat |
 * | 46 (A#2) | Open Hi-Hat |
 * | 39 (D#2) | Clap |
 * | 41 (F2) | Low Tom |
 * | 45 (A2) | Mid Tom |
 * | 48 (C3) | High Tom |
 * | 49 (C#3) | Crash |
 * | 51 (D#3) | Ride |
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | volume | float | 0-1 | 0.8 | Master output volume |
 * | kickVol | float | 0-1 | 1.0 | Kick drum volume |
 * | snareVol | float | 0-1 | 1.0 | Snare drum volume |
 * | hihatVol | float | 0-1 | 0.8 | Hi-hat volume |
 * | clapVol | float | 0-1 | 0.9 | Clap volume |
 * | tomVol | float | 0-1 | 0.9 | Toms volume |
 * | cymbalVol | float | 0-1 | 0.7 | Cymbals volume |
 *
 * @par Example
 * @code
 * auto& clock = chain.add<Clock>("clock");
 * clock.bpm = 120.0f;
 * clock.division(ClockDiv::Sixteenth);
 *
 * auto& seq = chain.add<Sequencer>("seq");
 * seq.setTriggerSource("clock");
 *
 * auto& kit = chain.add<DrumKit>("drums");
 * seq.setTarget("drums");  // Route sequencer to drum kit
 *
 * // Program a basic beat (using GM drum notes)
 * seq.setStep(0, 36, 1.0f);   // Kick on 1
 * seq.setStep(4, 38, 0.9f);   // Snare on 2
 * seq.setStep(8, 36, 0.8f);   // Kick on 3
 * seq.setStep(12, 38, 0.9f);  // Snare on 4
 *
 * // Hi-hats on every beat
 * for (int i = 0; i < 16; i += 2) {
 *     seq.setStep(i, 42, 0.6f);  // Closed hi-hat
 * }
 *
 * auto& out = chain.add<AudioOutput>("out");
 * out.input("drums");
 * chain.audioOutput("out");
 * @endcode
 *
 * @see Sequencer, Kick, Snare, HiHat, Clap, Tom, Cymbal, MidiIn
 */
class DrumKit : public AudioOperator, public MidiReceiver {
public:
    // General MIDI drum note numbers
    static constexpr uint8_t GM_KICK = 36;        // C2
    static constexpr uint8_t GM_SNARE = 38;       // D2
    static constexpr uint8_t GM_SNARE_RIM = 40;   // E2 (side stick)
    static constexpr uint8_t GM_CLOSED_HIHAT = 42; // F#2
    static constexpr uint8_t GM_OPEN_HIHAT = 46;  // A#2
    static constexpr uint8_t GM_PEDAL_HIHAT = 44; // G#2
    static constexpr uint8_t GM_CLAP = 39;        // D#2
    static constexpr uint8_t GM_LOW_TOM = 41;     // F2
    static constexpr uint8_t GM_MID_TOM = 45;     // A2
    static constexpr uint8_t GM_HIGH_TOM = 48;    // C3
    static constexpr uint8_t GM_CRASH = 49;       // C#3
    static constexpr uint8_t GM_RIDE = 51;        // D#3

    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> volume{"volume", 0.8f, 0.0f, 1.0f};       ///< Master volume
    Param<float> kickVol{"kickVol", 1.0f, 0.0f, 1.0f};     ///< Kick volume
    Param<float> snareVol{"snareVol", 1.0f, 0.0f, 1.0f};   ///< Snare volume
    Param<float> hihatVol{"hihatVol", 0.8f, 0.0f, 1.0f};   ///< Hi-hat volume
    Param<float> clapVol{"clapVol", 0.9f, 0.0f, 1.0f};     ///< Clap volume
    Param<float> tomVol{"tomVol", 0.9f, 0.0f, 1.0f};       ///< Toms volume
    Param<float> cymbalVol{"cymbalVol", 0.7f, 0.0f, 1.0f}; ///< Cymbals volume

    /// @}
    // -------------------------------------------------------------------------

    DrumKit();
    ~DrumKit() override;

    // -------------------------------------------------------------------------
    /// @name Drum Access
    /// @{

    /**
     * @brief Get access to the kick drum for parameter tweaking
     */
    Kick& kick() { return *m_kick; }
    const Kick& kick() const { return *m_kick; }

    /**
     * @brief Get access to the snare drum
     */
    Snare& snare() { return *m_snare; }
    const Snare& snare() const { return *m_snare; }

    /**
     * @brief Get access to the closed hi-hat
     */
    HiHat& closedHiHat() { return *m_closedHiHat; }
    const HiHat& closedHiHat() const { return *m_closedHiHat; }

    /**
     * @brief Get access to the open hi-hat
     */
    HiHat& openHiHat() { return *m_openHiHat; }
    const HiHat& openHiHat() const { return *m_openHiHat; }

    /**
     * @brief Get access to the clap
     */
    Clap& clap() { return *m_clap; }
    const Clap& clap() const { return *m_clap; }

    /**
     * @brief Get access to the low tom
     */
    Tom& lowTom() { return *m_lowTom; }
    const Tom& lowTom() const { return *m_lowTom; }

    /**
     * @brief Get access to the mid tom
     */
    Tom& midTom() { return *m_midTom; }
    const Tom& midTom() const { return *m_midTom; }

    /**
     * @brief Get access to the high tom
     */
    Tom& highTom() { return *m_highTom; }
    const Tom& highTom() const { return *m_highTom; }

    /**
     * @brief Get access to the crash cymbal
     */
    Cymbal& crash() { return *m_crash; }
    const Cymbal& crash() const { return *m_crash; }

    /**
     * @brief Get access to the ride cymbal
     */
    Cymbal& ride() { return *m_ride; }
    const Cymbal& ride() const { return *m_ride; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Note Mapping
    /// @{

    /**
     * @brief Set custom note mapping for a drum
     * @param note MIDI note number (0-127)
     * @param drum Drum type to trigger
     *
     * @par Example
     * @code
     * kit.setNoteMapping(60, DrumType::Kick);  // Map middle C to kick
     * @endcode
     */
    void setNoteMapping(uint8_t note, DrumType drum);

    /**
     * @brief Clear a note mapping
     */
    void clearNoteMapping(uint8_t note);

    /**
     * @brief Reset to default GM drum mapping
     */
    void resetNoteMappings();

    /// @}
    // -------------------------------------------------------------------------
    /// @name MidiReceiver Interface
    /// @{

    void midiNoteOn(uint8_t note, float velocity, uint8_t channel = 0) override;
    void midiNoteOff(uint8_t note, float velocity = 0.0f, uint8_t channel = 0) override;
    void midiAllNotesOff() override;
    void midiPanic() override;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Trigger a specific drum directly
     * @param drum Which drum to trigger
     * @param velocity Trigger velocity (0.0-1.0)
     */
    void triggerDrum(DrumType drum, float velocity = 1.0f);

    /**
     * @brief Check if any drum is currently sounding
     */
    bool isActive() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "DrumKit"; }
    InspectData inspect() const override;

    void generateBlock(uint32_t frameCount) override;

    bool drawVisualization(VizDrawList* drawList, float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

private:
    void initDrums();
    void setupDefaultMapping();
    AudioOperator* getDrumForType(DrumType type);
    float getVolumeForType(DrumType type);

    // Internal drum synthesizers
    std::unique_ptr<Kick> m_kick;
    std::unique_ptr<Snare> m_snare;
    std::unique_ptr<HiHat> m_closedHiHat;
    std::unique_ptr<HiHat> m_openHiHat;
    std::unique_ptr<Clap> m_clap;
    std::unique_ptr<Tom> m_lowTom;
    std::unique_ptr<Tom> m_midTom;
    std::unique_ptr<Tom> m_highTom;
    std::unique_ptr<Cymbal> m_crash;
    std::unique_ptr<Cymbal> m_ride;

    // Note-to-drum mapping
    std::array<DrumType, 128> m_noteMap;
    std::array<bool, 128> m_noteMapped;  // Whether note has a mapping

    // Velocity storage for visualization
    std::array<float, static_cast<size_t>(DrumType::Count)> m_lastVelocity = {};

    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
