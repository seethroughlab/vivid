#pragma once

/**
 * @file modulator.h
 * @brief Base classes for modulation sources (LFO, ADSR, Noise, etc.)
 *
 * Modulators can be used standalone as operators OR attached to synths
 * for per-voice modulation. This enables reusable modulation components
 * that work in both global and polyphonic contexts.
 *
 * Inspired by Bitwig's Unified Modulation System where modulators can
 * optionally operate per-voice with a simple toggle.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace vivid::audio {

/**
 * @brief Per-voice modulator state (owned by synth Voice struct)
 *
 * Each voice in a polyphonic synth maintains its own ModulatorState
 * for per-voice modulators. Global modulators share a single state.
 */
class ModulatorState {
public:
    virtual ~ModulatorState() = default;

    /**
     * @brief Reset state to initial values (called on noteOn for retrigger)
     */
    virtual void reset() = 0;
};

/**
 * @brief Base class for all modulation sources
 *
 * Modulators generate control signals that can be routed to parameters.
 * They support both standalone operation (as operators in the chain)
 * and attached operation (per-voice inside synths).
 *
 * @par Dual-Use Design
 * The same modulator class works in two contexts:
 * - **Standalone**: Added to chain with `chain.add<LFO>()`, runs once per block
 * - **Attached**: Added to synth with `synth.addModulator<LFO>()`, runs per-voice
 *
 * @par Per-Voice Mode
 * When `perVoice = true`, each synth voice maintains its own ModulatorState.
 * This enables effects like per-note filter envelopes or LFO phase per voice.
 *
 * @par Example
 * @code
 * // Standalone usage (global LFO for effects)
 * auto& globalLfo = chain.add<LFO>("globalLfo");
 * globalLfo.rate = 0.1f;
 *
 * // Attached usage (per-voice LFO inside synth)
 * auto& voiceLfo = synth.addModulator<LFO>("voiceLfo");
 * voiceLfo.rate = 5.0f;
 * voiceLfo.perVoice = true;
 * synth.modulate(voiceLfo, synth.filterCutoff, 0.5f);
 * @endcode
 */
class Modulator {
public:
    /**
     * @brief Per-voice mode toggle
     *
     * When true, each voice maintains its own state and the modulator
     * is processed per-voice. When false, all voices share one state.
     *
     * For envelopes, this should typically be true (each note has its own envelope).
     * For LFOs, either mode makes sense depending on the effect.
     */
    Param<bool> perVoice{"perVoice", false};

    /**
     * @brief Retrigger on noteOn
     *
     * When true, the modulator resets its phase/state on each noteOn.
     * For envelopes this is typically true. For free-running LFOs, false.
     */
    Param<bool> retrigger{"retrigger", true};

    virtual ~Modulator() = default;

    /**
     * @brief Get modulator name for display
     */
    virtual std::string modulatorName() const = 0;

    /**
     * @brief Create per-voice state instance
     * @return Unique pointer to new state (caller owns)
     *
     * Called by synth for each voice when perVoice=true.
     * The returned state is stored in the voice struct.
     */
    virtual std::unique_ptr<ModulatorState> createState() const = 0;

    /**
     * @brief Process one sample of modulation
     * @param state Per-voice state (may be shared for global modulators)
     * @param sampleRate Current sample rate
     * @return Modulation value (typically -1 to 1 or 0 to 1)
     */
    virtual float process(ModulatorState& state, float sampleRate) = 0;

    /**
     * @brief Called when a note starts
     * @param state Per-voice state to initialize
     *
     * Default behavior resets state if retrigger=true.
     */
    virtual void noteOn(ModulatorState& state) {
        if (static_cast<bool>(retrigger)) {
            state.reset();
        }
    }

    /**
     * @brief Called when a note is released
     * @param state Per-voice state
     *
     * Override for envelope-type modulators that need release phase.
     */
    virtual void noteOff(ModulatorState& state) {}

    /**
     * @brief Check if modulator is still active (for envelopes)
     * @param state Per-voice state
     * @return true if modulator is still producing output
     *
     * Used by synths to determine if a voice can be recycled.
     */
    virtual bool isActive(const ModulatorState& state) const { return true; }
};

/**
 * @brief Modulation routing entry
 *
 * Describes a connection from a modulator to a parameter with depth control.
 */
struct ModRoute {
    Modulator* source = nullptr;    ///< Source modulator
    std::string targetName;         ///< Target parameter name
    float depth = 1.0f;             ///< Modulation depth (-1 to 1 typical)
    bool bipolar = true;            ///< true: mod range is -1 to 1, false: 0 to 1

    ModRoute() = default;
    ModRoute(Modulator* src, const std::string& target, float d, bool bi = true)
        : source(src), targetName(target), depth(d), bipolar(bi) {}
};

/**
 * @brief Mixin for operators that can host modulators
 *
 * Provides infrastructure for adding modulators and routing them to parameters.
 * Synths inherit from this to gain modulator hosting capabilities.
 *
 * @par Usage
 * @code
 * class WavetableSynth : public AudioOperator, public ModulatorHost {
 *     void generateBlock(uint32_t frameCount) override {
 *         // In voice loop:
 *         for (auto& voice : m_voices) {
 *             // Get modulated parameter value
 *             float cutoff = getModulatedValue("filterCutoff", baseCutoff, voice.modStates);
 *         }
 *     }
 * };
 * @endcode
 */
class ModulatorHost {
public:
    virtual ~ModulatorHost() = default;

    /**
     * @brief Add a modulator to this host
     * @tparam T Modulator type (must derive from Modulator)
     * @param name Display name for the modulator
     * @return Reference to the created modulator
     *
     * @par Example
     * @code
     * auto& lfo = synth.addModulator<LFO>("filterLfo");
     * lfo.rate = 2.0f;
     * lfo.perVoice = true;
     * @endcode
     */
    template<typename T>
    T& addModulator(const std::string& name) {
        static_assert(std::is_base_of_v<Modulator, T>, "T must derive from Modulator");
        auto mod = std::make_unique<T>();
        T& ref = *mod;
        m_modulators[name] = std::move(mod);
        m_modulatorOrder.push_back(name);
        return ref;
    }

    /**
     * @brief Get a modulator by name
     * @tparam T Expected modulator type
     * @param name Modulator name
     * @return Pointer to modulator, or nullptr if not found/wrong type
     */
    template<typename T>
    T* getModulator(const std::string& name) {
        auto it = m_modulators.find(name);
        if (it != m_modulators.end()) {
            return dynamic_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    /**
     * @brief Route a modulator to a parameter
     * @param source Modulator to read from
     * @param targetParam Target parameter name (must match Param<T>::name())
     * @param depth Modulation depth (multiplied with modulator output)
     * @param bipolar true for bipolar mod (-1 to 1), false for unipolar (0 to 1)
     *
     * Multiple modulators can target the same parameter - they sum together.
     *
     * @par Example
     * @code
     * synth.modulate(lfo, "filterCutoff", 0.5f);      // LFO modulates cutoff
     * synth.modulate(filterEnv, "filterCutoff", 0.8f); // Envelope also modulates
     * @endcode
     */
    void modulate(Modulator& source, const std::string& targetParam,
                  float depth, bool bipolar = true) {
        m_modRoutes.push_back(ModRoute(&source, targetParam, depth, bipolar));
    }

    /**
     * @brief Remove all modulation routes to a parameter
     * @param targetParam Parameter name to clear
     */
    void clearModulation(const std::string& targetParam) {
        m_modRoutes.erase(
            std::remove_if(m_modRoutes.begin(), m_modRoutes.end(),
                [&](const ModRoute& r) { return r.targetName == targetParam; }),
            m_modRoutes.end()
        );
    }

    /**
     * @brief Get all modulation routes
     */
    const std::vector<ModRoute>& modRoutes() const { return m_modRoutes; }

    /**
     * @brief Get all modulators
     */
    const std::unordered_map<std::string, std::unique_ptr<Modulator>>& modulators() const {
        return m_modulators;
    }

    /**
     * @brief Get modulator names in order added
     */
    const std::vector<std::string>& modulatorOrder() const { return m_modulatorOrder; }

protected:
    /**
     * @brief Per-voice modulator state storage
     *
     * Each voice should have one of these to store per-voice mod states.
     */
    using VoiceModStates = std::unordered_map<Modulator*, std::unique_ptr<ModulatorState>>;

    /**
     * @brief Create per-voice states for all perVoice modulators
     * @param[out] states Map to populate with modulator states
     */
    void createVoiceModStates(VoiceModStates& states) const {
        for (const auto& [name, mod] : m_modulators) {
            if (static_cast<bool>(mod->perVoice)) {
                states[mod.get()] = mod->createState();
            }
        }
    }

    /**
     * @brief Process all modulators for a voice
     * @param voiceStates Per-voice modulator states
     * @param globalState Shared state for global modulators
     * @param sampleRate Current sample rate
     * @param[out] modValues Output map: modulator -> current value
     */
    void processModulators(VoiceModStates& voiceStates,
                           VoiceModStates& globalState,
                           float sampleRate,
                           std::unordered_map<Modulator*, float>& modValues) {
        for (const auto& [name, mod] : m_modulators) {
            ModulatorState* state = static_cast<bool>(mod->perVoice)
                ? voiceStates[mod.get()].get()
                : globalState[mod.get()].get();
            if (state) {
                modValues[mod.get()] = mod->process(*state, sampleRate);
            }
        }
    }

    /**
     * @brief Calculate total modulation for a parameter
     * @param targetParam Parameter name
     * @param baseValue Base parameter value (before modulation)
     * @param modValues Current modulator output values
     * @return Modulated value
     *
     * Sums all modulation routes targeting this parameter.
     * For bipolar mods, output is baseValue + sum(modValue * depth * range)
     * For unipolar mods, modValue is remapped from -1..1 to 0..1 first.
     */
    float applyModulation(const std::string& targetParam, float baseValue,
                          const std::unordered_map<Modulator*, float>& modValues,
                          float paramMin, float paramMax) const {
        float totalMod = 0.0f;
        float range = paramMax - paramMin;

        for (const auto& route : m_modRoutes) {
            if (route.targetName == targetParam && route.source) {
                auto it = modValues.find(route.source);
                if (it != modValues.end()) {
                    float modValue = it->second;  // -1 to 1 or 0 to 1
                    if (!route.bipolar) {
                        modValue = (modValue + 1.0f) * 0.5f;  // Convert to 0-1
                    }
                    totalMod += modValue * route.depth * range;
                }
            }
        }

        return std::clamp(baseValue + totalMod, paramMin, paramMax);
    }

    /**
     * @brief Trigger noteOn for all modulators
     * @param voiceStates Per-voice modulator states
     * @param globalState Global modulator states (for non-perVoice mods)
     */
    void modulatorsNoteOn(VoiceModStates& voiceStates, VoiceModStates& globalState) {
        for (const auto& [name, mod] : m_modulators) {
            ModulatorState* state = static_cast<bool>(mod->perVoice)
                ? voiceStates[mod.get()].get()
                : globalState[mod.get()].get();
            if (state) {
                mod->noteOn(*state);
            }
        }
    }

    /**
     * @brief Trigger noteOff for all modulators
     * @param voiceStates Per-voice modulator states
     * @param globalState Global modulator states
     */
    void modulatorsNoteOff(VoiceModStates& voiceStates, VoiceModStates& globalState) {
        for (const auto& [name, mod] : m_modulators) {
            ModulatorState* state = static_cast<bool>(mod->perVoice)
                ? voiceStates[mod.get()].get()
                : globalState[mod.get()].get();
            if (state) {
                mod->noteOff(*state);
            }
        }
    }

private:
    std::unordered_map<std::string, std::unique_ptr<Modulator>> m_modulators;
    std::vector<std::string> m_modulatorOrder;
    std::vector<ModRoute> m_modRoutes;
};

} // namespace vivid::audio
