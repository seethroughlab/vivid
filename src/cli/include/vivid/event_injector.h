// Event Injector — Reads a JSON timeline script and injects synthetic events
// into Context/Chain at the right frames for puppeteered playback & export.

#pragma once

#include <vivid/easing.h>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace vivid {

class Context;
class Chain;

/// A single event in a playback script
struct ScriptEvent {
    int frame = 0;           // Frame to fire at
    int endFrame = -1;       // For ramps: end frame
    std::string type;        // "param_set", "param_ramp", "key_press", etc.
    std::string op;          // Target operator name
    std::string param;       // Parameter name
    float value = 0.0f;      // Value (or "from" for ramps)
    float valueTo = 0.0f;    // "to" for ramps
    int note = 0;            // MIDI note
    float velocity = 1.0f;   // MIDI velocity
    float x = 0.0f, y = 0.0f;  // Mouse position (normalized 0-1)
    std::string key;         // Key name for key_press/key_release
    int button = 0;          // Mouse button index (0=left, 1=right, 2=middle)
    int cc = 0;              // MIDI CC number
    int channel = 0;         // MIDI channel
    std::string easing;      // Easing curve name (for param_ramp and snapshot_recall)
};

/// Top-level settings from a playback script JSON
struct PlaybackScript {
    float duration = 0.0f;   // Duration in seconds (0 = not specified)
    float fps = 60.0f;       // Frame rate
    int width = 0, height = 0;  // Resolution (0 = use project default)
    std::string codec;       // "h264", "h265", "prores"
    bool audio = false;      // Include audio track
    std::vector<ScriptEvent> events;
};

/// Reads a JSON timeline script and injects events into Context/Chain each frame.
class EventInjector {
public:
    /// Load script from a JSON file. Returns true on success.
    bool load(const std::string& path);

    /// Get the loaded script settings.
    const PlaybackScript& script() const { return m_script; }

    /// Process all events for the given frame.
    /// Called once per frame in the main loop.
    void processFrame(int frame, Context& ctx, Chain& chain);

    /// Validate script against a loaded chain.
    /// Returns warnings for events targeting nonexistent operators/params.
    std::vector<std::string> validate(const Chain& chain) const;

    /// Error string from last failed load().
    const std::string& error() const { return m_error; }

    /// Returns true if processFrame() injected a mouse position this frame.
    bool mouseWasInjected() const { return m_mouseInjectedThisFrame; }

    /// Returns the last injected mouse position (in framebuffer/pixel coords).
    glm::vec2 lastInjectedMousePos() const { return m_lastInjectedMousePos; }

private:
    PlaybackScript m_script;
    std::string m_error;

    // Active ramps being interpolated
    struct ActiveRamp {
        std::string op;
        std::string param;
        int startFrame;
        int endFrame;
        float from;
        float to;
        EasingCurve easingCurve;
    };
    std::vector<ActiveRamp> m_activeRamps;

    // Mouse buttons pending release (auto-release on next frame after mouse_click)
    std::vector<int> m_pendingMouseRelease;

    // Track whether mouse position was injected this frame
    bool m_mouseInjectedThisFrame = false;
    glm::vec2 m_lastInjectedMousePos = {0, 0};

    /// Convert a key name string (e.g. "space", "a") to GLFW keycode.
    static int keyNameToGLFW(const std::string& name);
};

} // namespace vivid
