#pragma once
/**
 * @file midi_mapping.h
 * @brief Akai MIDImix CC assignments and parameter scaling
 *
 * All MIDI controller configuration in one place. This file can be
 * adapted for other controllers by changing the CC numbers.
 *
 * MIDImix Layout:
 * - 8 channel strips, each with 3 knobs + mute + solo + fader
 * - Sends on MIDI Channel 1 by default
 */

#include <cstdint>

namespace prelinger::midi {

// =========================================================================
// Akai MIDImix CC Assignments
// =========================================================================

// Faders (bottom row) - Mix levels
namespace Fader {
    inline constexpr uint8_t PAD      = 19;  // Ch1: PolySynth pad
    inline constexpr uint8_t LEAD     = 23;  // Ch2: WavetableSynth lead
    inline constexpr uint8_t BELLS    = 27;  // Ch3: FMSynth bells
    inline constexpr uint8_t CLOUDS   = 31;  // Ch4: Granular atmosphere
    inline constexpr uint8_t DELAY    = 49;  // Ch5: Delay wet/dry
    inline constexpr uint8_t REVERB   = 53;  // Ch6: Reverb wet/dry
    inline constexpr uint8_t TAPE     = 57;  // Ch7: Tape saturation
    inline constexpr uint8_t MASTER   = 61;  // Ch8: Master output
}

// Knob Row 1 - Filters & Time-based Effects
namespace Knob1 {
    inline constexpr uint8_t PAD_CUTOFF     = 16;  // Pad filter cutoff
    inline constexpr uint8_t PAD_RESO       = 20;  // Pad filter resonance
    inline constexpr uint8_t LEAD_CUTOFF    = 24;  // Lead filter cutoff
    inline constexpr uint8_t LEAD_RESO      = 28;  // Lead filter resonance
    inline constexpr uint8_t DELAY_TIME     = 46;  // Delay time
    inline constexpr uint8_t DELAY_FB       = 50;  // Delay feedback
    inline constexpr uint8_t REVERB_SIZE    = 54;  // Reverb room size
    inline constexpr uint8_t REVERB_DAMP    = 58;  // Reverb damping
}

// Knob Row 2 - Texture Effects
namespace Knob2 {
    inline constexpr uint8_t TAPE_WOW       = 17;  // Tape wow (slow pitch)
    inline constexpr uint8_t TAPE_FLUTTER   = 21;  // Tape flutter (fast pitch)
    inline constexpr uint8_t TAPE_HISS      = 25;  // Tape hiss level
    inline constexpr uint8_t TAPE_AGE       = 29;  // Tape degradation
    inline constexpr uint8_t GRAIN_POS      = 47;  // Granular position
    inline constexpr uint8_t GRAIN_DENSITY  = 51;  // Granular density
    inline constexpr uint8_t GRAIN_PITCH    = 55;  // Granular pitch
    inline constexpr uint8_t GRAIN_SPRAY    = 59;  // Granular position spray
}

// Knob Row 3 - Visual Effects
namespace Knob3 {
    inline constexpr uint8_t BLOOM_INT      = 18;  // Bloom intensity
    inline constexpr uint8_t BLOOM_THRESH   = 22;  // Bloom threshold
    inline constexpr uint8_t FB_DECAY       = 26;  // Feedback decay
    inline constexpr uint8_t FB_ZOOM        = 30;  // Feedback zoom
    inline constexpr uint8_t GRAIN_INT      = 48;  // Film grain intensity
    inline constexpr uint8_t HSV_SAT        = 52;  // Color saturation
    inline constexpr uint8_t HSV_HUE        = 56;  // Hue shift
    inline constexpr uint8_t VIDEO_SPEED    = 60;  // Video playback speed
}

// Mute Buttons - Toggles
namespace Mute {
    inline constexpr uint8_t GRAIN_TOGGLE   = 1;   // Toggle film grain
    inline constexpr uint8_t CRT_TOGGLE     = 4;   // Toggle CRT effect
    inline constexpr uint8_t FB_TOGGLE      = 7;   // Toggle feedback
    inline constexpr uint8_t FLASH          = 10;  // Trigger flash
    inline constexpr uint8_t PAUSE          = 13;  // Pause/resume
    inline constexpr uint8_t FREEZE         = 16;  // Freeze granular
}

// Solo Buttons - Section Navigation & Triggers
namespace Solo {
    inline constexpr uint8_t PREV_SECTION   = 2;   // Go to previous section
    inline constexpr uint8_t NEXT_SECTION   = 5;   // Go to next section
    inline constexpr uint8_t RESTART        = 8;   // Restart from intro
    inline constexpr uint8_t SKIP_TO_CHORUS = 11;  // Jump to chorus
    inline constexpr uint8_t BELLS          = 14;  // Trigger bell chord
}

// =========================================================================
// Parameter Scaling Functions
// =========================================================================
// All functions take CC value (0.0-1.0) and return scaled parameter value

// Pad filter cutoff: 200-4000 Hz (warm range)
inline float scalePadCutoff(float cc) {
    return 200.0f + cc * 3800.0f;
}

// Lead filter cutoff: 500-5000 Hz (brighter range)
inline float scaleLeadCutoff(float cc) {
    return 500.0f + cc * 4500.0f;
}

// Delay time: 100-1000 ms
inline float scaleDelayTime(float cc) {
    return 100.0f + cc * 900.0f;
}

// Feedback: 0-90% (avoid runaway)
inline float scaleFeedback(float cc) {
    return cc * 0.9f;
}

// Hiss: 0-50% (subtle range)
inline float scaleHiss(float cc) {
    return cc * 0.5f;
}

// Granular density: 1-50 grains/s
inline float scaleDensity(float cc) {
    return 1.0f + cc * 49.0f;
}

// Granular pitch: 0.25-2.0x
inline float scalePitch(float cc) {
    return 0.25f + cc * 1.75f;
}

// Bloom: 0-300%
inline float scaleBloom(float cc) {
    return cc * 3.0f;
}

// Feedback decay: 80-99%
inline float scaleFbDecay(float cc) {
    return 0.8f + cc * 0.19f;
}

// Feedback zoom: 0.99-1.02
inline float scaleFbZoom(float cc) {
    return 0.99f + cc * 0.03f;
}

// Film grain: 0-50%
inline float scaleFilmGrain(float cc) {
    return cc * 0.5f;
}

// Hue shift: -0.5 to +0.5
inline float scaleHue(float cc) {
    return -0.5f + cc;
}

// Video speed: 0.5-1.5x
inline float scaleVideoSpeed(float cc) {
    return 0.5f + cc;
}

} // namespace prelinger::midi
