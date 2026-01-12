#pragma once

/**
 * @file trigger.h
 * @brief Trigger utility operator for event-driven effects
 *
 * A simple envelope generator that can be triggered programmatically.
 * Perfect for connecting external events (MIDI, OSC) to visual effects.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/param_registry.h>
#include <vivid/operator_registry.h>

namespace vivid::effects {

/**
 * @brief Trigger utility with attack/decay envelope
 *
 * A utility operator that generates a 0-1 value with configurable
 * attack and decay. Use it to convert discrete events (MIDI notes,
 * OSC messages, button presses) into smooth envelopes that can
 * drive visual parameters.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | attack | float | 0-1 | 0.0 | Attack time (0 = instant) |
 * | decay | float | 0.5-0.999 | 0.92 | Decay rate per frame |
 *
 * @par Example: MIDI-triggered flash
 * @code
 * // Setup
 * auto& midiIn = chain.add<MidiIn>("midi");
 * midiIn.openPortByName("Arturia");
 *
 * auto& kickTrigger = chain.add<Trigger>("kick");
 * kickTrigger.decay = 0.85f;  // Fast decay for punchy response
 *
 * // Update
 * if (midiIn.noteOn(36)) {  // MIDI note 36 = kick drum
 *     kickTrigger.fire();
 * }
 *
 * // Use trigger value to drive effect intensity
 * auto& bloom = chain.get<Bloom>("bloom");
 * bloom.intensity = 0.2f + kickTrigger.value() * 0.8f;
 * @endcode
 *
 * @par Example: Multiple triggers for drums
 * @code
 * auto& kick = chain.add<Trigger>("kick");
 * auto& snare = chain.add<Trigger>("snare");
 * auto& hihat = chain.add<Trigger>("hihat");
 *
 * kick.decay = 0.85f;   // Punchy
 * snare.decay = 0.9f;   // Medium
 * hihat.decay = 0.95f;  // Sustain
 *
 * // In update:
 * for (const auto& e : midiIn.events()) {
 *     if (e.type == MidiEventType::NoteOn) {
 *         if (e.note == 36) kick.fire(e.velocity / 127.0f);
 *         if (e.note == 38) snare.fire(e.velocity / 127.0f);
 *         if (e.note == 42) hihat.fire(e.velocity / 127.0f);
 *     }
 * }
 * @endcode
 *
 * @see Flash, MidiIn, OscIn, BeatDetect
 */
class Trigger : public Operator, public ParamRegistry {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    /// Attack time (0 = instant, 1 = ~1 second ramp-up)
    Param<float> attack{"attack", 0.0f, 0.0f, 1.0f};

    /// Decay rate per frame (0.8 = fast, 0.99 = slow trails)
    Param<float> decay{"decay", 0.92f, 0.5f, 0.999f};

    /// @}
    // -------------------------------------------------------------------------

    Trigger();
    ~Trigger() override = default;

    // -------------------------------------------------------------------------
    /// @name Triggering
    /// @{

    /// @brief Fire the trigger at full intensity (1.0)
    void fire() { fire(1.0f); }

    /// @brief Fire the trigger with custom intensity (0-1)
    void fire(float intensity);

    /// @brief Get current envelope value (0-1)
    [[nodiscard]] float value() const { return m_value; }

    /// @brief Check if trigger is active (value > 0.001)
    [[nodiscard]] bool active() const { return m_value > 0.001f; }

    /// @brief Reset trigger to zero
    void reset() { m_value = 0.0f; m_target = 0.0f; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    std::string name() const override { return "Trigger"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    std::vector<ParamDecl> params() override { return registeredParams(); }
    bool getParam(const std::string& name, float out[4]) override {
        return getRegisteredParam(name, out);
    }
    bool setParam(const std::string& name, const float value[4]) override {
        return setRegisteredParam(name, value);
    }

    /// @}

private:
    float m_value = 0.0f;   ///< Current envelope value
    float m_target = 0.0f;  ///< Target value (for attack phase)
};

} // namespace vivid::effects
