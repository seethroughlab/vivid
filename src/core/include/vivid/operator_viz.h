#pragma once

/**
 * @file operator_viz.h
 * @brief Visualization data for operators
 *
 * Operators can override getVisualizationData() to provide hints
 * about how to visualize them. The chain visualizer renders based
 * on the data type - no registration or addon coupling needed.
 */

#include <cstdint>

namespace vivid {

/**
 * @brief Visualization data returned by operators
 *
 * Operators override getVisualizationData() to return this struct.
 * The chain visualizer uses the type to select a renderer.
 */
struct OperatorVizData {
    /**
     * @brief Visualization type - determines how to render
     */
    enum class Type {
        Default,        ///< Use default visualization (waveform for audio)
        DrumEnvelope,   ///< Drum-style envelope (amplitude + optional pitch)
        DualEnvelope,   ///< Two envelopes (e.g., tone + noise for snare)
        SynthADSR,      ///< ADSR envelope with waveform icon
        VoiceActivity,  ///< Polyphonic voice indicators + envelope
        GainReduction,  ///< Compressor/limiter gain reduction meter
        Gate,           ///< Gate open/closed state
        FreqResponse,   ///< Filter frequency response curve
        FMAlgorithm,    ///< FM synth operator routing diagram
    };

    Type type = Type::Default;

    // -------------------------------------------------------------------------
    // DrumEnvelope data
    float ampEnvelope = 0;      ///< Amplitude envelope value (0-1)
    float pitchEnvelope = 0;    ///< Pitch envelope value (0-1), 0 = no pitch viz

    // -------------------------------------------------------------------------
    // DualEnvelope data (uses ampEnvelope for first, pitchEnvelope for second)
    // Reuses ampEnvelope and pitchEnvelope fields

    // -------------------------------------------------------------------------
    // SynthADSR data
    float attack = 0.01f;       ///< Attack time (seconds)
    float decay = 0.1f;         ///< Decay time (seconds)
    float sustain = 0.7f;       ///< Sustain level (0-1)
    float release = 0.3f;       ///< Release time (seconds)
    float envelopeValue = 0;    ///< Current envelope value (0-1)
    int waveformType = 0;       ///< 0=Sine, 1=Square, 2=Saw, 3=Triangle, 4=Pulse

    // -------------------------------------------------------------------------
    // VoiceActivity data
    int activeVoices = 0;       ///< Number of currently active voices
    int maxVoices = 8;          ///< Maximum voice count
    float maxEnvelopeValue = 0; ///< Max envelope across all voices

    // -------------------------------------------------------------------------
    // GainReduction data
    float gainReductionDb = 0;  ///< Gain reduction in dB (negative)
    float thresholdDb = -12;    ///< Threshold in dB

    // -------------------------------------------------------------------------
    // Gate data
    bool gateOpen = false;      ///< Whether gate is open
    float gateGain = 0;         ///< Current gate gain (0-1)

    // -------------------------------------------------------------------------
    // FreqResponse data
    float cutoffHz = 1000;      ///< Filter cutoff frequency
    float resonance = 0.707f;   ///< Filter Q/resonance
    int filterType = 0;         ///< 0=LP, 1=HP, 2=BP, 3=Notch, 4=LShelf, 5=HShelf, 6=Peak

    // -------------------------------------------------------------------------
    // FMAlgorithm data
    int fmAlgorithm = 0;        ///< Algorithm index (0-7)
    float opEnvelope[4] = {0};  ///< Per-operator envelope values

    // -------------------------------------------------------------------------
    // Display hints
    uint32_t bgColor = 0;       ///< Background color (ABGR), 0 = use default
};

} // namespace vivid
