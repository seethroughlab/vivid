#pragma once

/**
 * @file midi_receiver.h
 * @brief Interface for MIDI-playable operators
 *
 * Any operator that can receive MIDI events (synths, samplers, future plugins)
 * implements this interface for automatic routing from MidiIn.
 */

#include <cstdint>

namespace vivid::audio {

/**
 * @brief Interface for MIDI-playable operators
 *
 * Implement this interface to receive MIDI events from MidiIn via setTarget().
 * Enables native MIDI routing without manual event polling.
 *
 * @par Example Implementation
 * @code
 * class MySynth : public AudioOperator, public MidiReceiver {
 * public:
 *     void midiNoteOn(uint8_t note, float velocity, uint8_t channel = 0) override {
 *         noteOnMidi(note, velocity);
 *     }
 *
 *     void midiNoteOff(uint8_t note, float velocity = 0.0f, uint8_t channel = 0) override {
 *         noteOffMidi(note);
 *     }
 *
 *     void midiPitchBend(float value, uint8_t channel = 0) override {
 *         m_pitchBend = value;
 *     }
 * };
 * @endcode
 *
 * @par Usage
 * @code
 * auto& midi = chain.add<MidiIn>("midi");
 * auto& synth = chain.add<PolySynth>("synth");
 * midi.setTarget("synth");  // Notes now auto-route to synth
 * @endcode
 *
 * @see MidiIn, PolySynth, WavetableSynth, FMSynth, Synth, Sampler
 */
class MidiReceiver {
public:
    virtual ~MidiReceiver() = default;

    // -------------------------------------------------------------------------
    /// @name Required Methods
    /// @{

    /**
     * @brief Handle MIDI note-on event
     * @param note MIDI note number (0-127, 60 = middle C)
     * @param velocity Note velocity (0.0-1.0)
     * @param channel MIDI channel (0-15, default 0)
     */
    virtual void midiNoteOn(uint8_t note, float velocity, uint8_t channel = 0) = 0;

    /**
     * @brief Handle MIDI note-off event
     * @param note MIDI note number (0-127)
     * @param velocity Release velocity (0.0-1.0, often ignored)
     * @param channel MIDI channel (0-15, default 0)
     */
    virtual void midiNoteOff(uint8_t note, float velocity = 0.0f, uint8_t channel = 0) = 0;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Optional Methods (defaults provided)
    /// @{

    /**
     * @brief Handle MIDI pitch bend event
     * @param value Pitch bend amount (-1.0 to +1.0, 0.0 = center)
     * @param channel MIDI channel (0-15, default 0)
     *
     * Default implementation does nothing. Override to apply pitch bend
     * to all playing voices.
     */
    virtual void midiPitchBend(float value, uint8_t channel = 0) {
        (void)value;
        (void)channel;
    }

    /**
     * @brief Handle MIDI control change event
     * @param cc Controller number (0-127)
     * @param value Controller value (0.0-1.0)
     * @param channel MIDI channel (0-15, default 0)
     *
     * Default implementation does nothing. Override to handle CCs directly
     * (e.g., mod wheel affecting filter cutoff).
     */
    virtual void midiCC(uint8_t cc, float value, uint8_t channel = 0) {
        (void)cc;
        (void)value;
        (void)channel;
    }

    /**
     * @brief Release all playing notes (MIDI All Notes Off)
     *
     * Default implementation does nothing. Override to gracefully
     * release all voices (triggers release envelopes).
     */
    virtual void midiAllNotesOff() {}

    /**
     * @brief Immediately silence all voices (MIDI Panic)
     *
     * Default implementation calls midiAllNotesOff(). Override for
     * instant silence without release envelopes.
     */
    virtual void midiPanic() { midiAllNotesOff(); }

    /// @}
};

} // namespace vivid::audio
