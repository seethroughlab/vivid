// Event Injector — JSON playback script loading, frame processing, key mapping

#include <vivid/event_injector.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <vivid/operator.h>

#include <nlohmann/json.hpp>
#include <GLFW/glfw3.h>

#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <iostream>

using json = nlohmann::json;

namespace vivid {

// Key name → GLFW keycode lookup table
static const std::unordered_map<std::string, int>& keyMap() {
    static const std::unordered_map<std::string, int> map = {
        // Letters
        {"a", GLFW_KEY_A}, {"b", GLFW_KEY_B}, {"c", GLFW_KEY_C}, {"d", GLFW_KEY_D},
        {"e", GLFW_KEY_E}, {"f", GLFW_KEY_F}, {"g", GLFW_KEY_G}, {"h", GLFW_KEY_H},
        {"i", GLFW_KEY_I}, {"j", GLFW_KEY_J}, {"k", GLFW_KEY_K}, {"l", GLFW_KEY_L},
        {"m", GLFW_KEY_M}, {"n", GLFW_KEY_N}, {"o", GLFW_KEY_O}, {"p", GLFW_KEY_P},
        {"q", GLFW_KEY_Q}, {"r", GLFW_KEY_R}, {"s", GLFW_KEY_S}, {"t", GLFW_KEY_T},
        {"u", GLFW_KEY_U}, {"v", GLFW_KEY_V}, {"w", GLFW_KEY_W}, {"x", GLFW_KEY_X},
        {"y", GLFW_KEY_Y}, {"z", GLFW_KEY_Z},
        // Numbers
        {"0", GLFW_KEY_0}, {"1", GLFW_KEY_1}, {"2", GLFW_KEY_2}, {"3", GLFW_KEY_3},
        {"4", GLFW_KEY_4}, {"5", GLFW_KEY_5}, {"6", GLFW_KEY_6}, {"7", GLFW_KEY_7},
        {"8", GLFW_KEY_8}, {"9", GLFW_KEY_9},
        // Special keys
        {"space", GLFW_KEY_SPACE}, {"enter", GLFW_KEY_ENTER}, {"return", GLFW_KEY_ENTER},
        {"escape", GLFW_KEY_ESCAPE}, {"esc", GLFW_KEY_ESCAPE},
        {"tab", GLFW_KEY_TAB}, {"backspace", GLFW_KEY_BACKSPACE},
        {"delete", GLFW_KEY_DELETE}, {"insert", GLFW_KEY_INSERT},
        // Arrow keys
        {"up", GLFW_KEY_UP}, {"down", GLFW_KEY_DOWN},
        {"left", GLFW_KEY_LEFT}, {"right", GLFW_KEY_RIGHT},
        // Modifiers
        {"shift", GLFW_KEY_LEFT_SHIFT}, {"ctrl", GLFW_KEY_LEFT_CONTROL},
        {"control", GLFW_KEY_LEFT_CONTROL}, {"alt", GLFW_KEY_LEFT_ALT},
        {"super", GLFW_KEY_LEFT_SUPER}, {"command", GLFW_KEY_LEFT_SUPER},
        // Function keys
        {"f1", GLFW_KEY_F1}, {"f2", GLFW_KEY_F2}, {"f3", GLFW_KEY_F3},
        {"f4", GLFW_KEY_F4}, {"f5", GLFW_KEY_F5}, {"f6", GLFW_KEY_F6},
        {"f7", GLFW_KEY_F7}, {"f8", GLFW_KEY_F8}, {"f9", GLFW_KEY_F9},
        {"f10", GLFW_KEY_F10}, {"f11", GLFW_KEY_F11}, {"f12", GLFW_KEY_F12},
        // Misc
        {"home", GLFW_KEY_HOME}, {"end", GLFW_KEY_END},
        {"pageup", GLFW_KEY_PAGE_UP}, {"pagedown", GLFW_KEY_PAGE_DOWN},
        {"minus", GLFW_KEY_MINUS}, {"equal", GLFW_KEY_EQUAL},
        {"comma", GLFW_KEY_COMMA}, {"period", GLFW_KEY_PERIOD},
        {"slash", GLFW_KEY_SLASH}, {"semicolon", GLFW_KEY_SEMICOLON},
    };
    return map;
}

int EventInjector::keyNameToGLFW(const std::string& name) {
    // Convert to lowercase for case-insensitive lookup
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    auto& map = keyMap();
    auto it = map.find(lower);
    if (it != map.end()) {
        return it->second;
    }
    return GLFW_KEY_UNKNOWN;
}

bool EventInjector::load(const std::string& path) {
    m_script = PlaybackScript{};
    m_activeRamps.clear();
    m_error.clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        m_error = "Cannot open script file: " + path;
        return false;
    }

    json j;
    try {
        j = json::parse(file);
    } catch (const json::parse_error& e) {
        m_error = "JSON parse error: " + std::string(e.what());
        return false;
    }

    // Parse top-level settings
    if (j.contains("duration") && j["duration"].is_number()) {
        m_script.duration = j["duration"].get<float>();
    }
    if (j.contains("fps") && j["fps"].is_number()) {
        m_script.fps = j["fps"].get<float>();
    }
    if (j.contains("resolution") && j["resolution"].is_array() && j["resolution"].size() == 2) {
        m_script.width = j["resolution"][0].get<int>();
        m_script.height = j["resolution"][1].get<int>();
    }
    if (j.contains("codec") && j["codec"].is_string()) {
        m_script.codec = j["codec"].get<std::string>();
    }
    if (j.contains("audio") && j["audio"].is_boolean()) {
        m_script.audio = j["audio"].get<bool>();
    }

    // Parse events
    if (j.contains("events") && j["events"].is_array()) {
        for (const auto& ev : j["events"]) {
            ScriptEvent event;

            if (!ev.contains("frame") || !ev.contains("type")) {
                continue;  // Skip malformed events
            }

            event.frame = ev["frame"].get<int>();
            event.type = ev["type"].get<std::string>();

            if (ev.contains("operator")) event.op = ev["operator"].get<std::string>();
            if (ev.contains("param")) event.param = ev["param"].get<std::string>();
            if (ev.contains("value")) event.value = ev["value"].get<float>();
            if (ev.contains("from")) event.value = ev["from"].get<float>();
            if (ev.contains("to")) event.valueTo = ev["to"].get<float>();
            if (ev.contains("end_frame")) event.endFrame = ev["end_frame"].get<int>();
            if (ev.contains("note")) event.note = ev["note"].get<int>();
            if (ev.contains("velocity")) event.velocity = ev["velocity"].get<float>();
            if (ev.contains("x")) event.x = ev["x"].get<float>();
            if (ev.contains("y")) event.y = ev["y"].get<float>();
            if (ev.contains("key")) event.key = ev["key"].get<std::string>();

            m_script.events.push_back(event);
        }
    }

    // Sort events by frame for efficient processing
    std::sort(m_script.events.begin(), m_script.events.end(),
              [](const ScriptEvent& a, const ScriptEvent& b) {
                  return a.frame < b.frame;
              });

    return true;
}

void EventInjector::processFrame(int frame, Context& ctx, Chain& chain) {
    // Process instant events that fire on this frame
    for (const auto& ev : m_script.events) {
        if (ev.frame > frame) break;  // Events are sorted, no more for this frame
        if (ev.frame < frame) continue;  // Already past

        if (ev.type == "param_set") {
            Operator* op = chain.getByName(ev.op);
            if (op) {
                float val[4] = {ev.value, 0, 0, 0};
                op->setParam(ev.param, val);
                op->markDirty();
            }
        } else if (ev.type == "param_ramp") {
            // Start a ramp — set initial value and register for interpolation
            Operator* op = chain.getByName(ev.op);
            if (op) {
                float val[4] = {ev.value, 0, 0, 0};
                op->setParam(ev.param, val);
                op->markDirty();
            }
            m_activeRamps.push_back({ev.op, ev.param, ev.frame, ev.endFrame, ev.value, ev.valueTo});
        } else if (ev.type == "key_press") {
            int keycode = keyNameToGLFW(ev.key);
            if (keycode != GLFW_KEY_UNKNOWN) {
                ctx.injectKeyState(keycode, true);
            }
        } else if (ev.type == "key_release") {
            int keycode = keyNameToGLFW(ev.key);
            if (keycode != GLFW_KEY_UNKNOWN) {
                ctx.injectKeyState(keycode, false);
            }
        } else if (ev.type == "trigger") {
            // Try to trigger via MidiReceiver interface (noteOn with default velocity)
            Operator* op = chain.getByName(ev.op);
            if (op) {
                // Use setParam with a special "trigger" param as a generic trigger mechanism
                float val[4] = {1.0f, 0, 0, 0};
                op->setParam("trigger", val);
                op->markDirty();
            }
        } else if (ev.type == "midi_note") {
            // MIDI note on — handled via dynamic_cast in the audio module
            // We can't include audio headers here, so use setParam as fallback
            Operator* op = chain.getByName(ev.op);
            if (op) {
                // Try setParam("midi_note_on", [note, velocity, 0, 0])
                float val[4] = {static_cast<float>(ev.note), ev.velocity, 0, 0};
                op->setParam("midi_note_on", val);
            }
        } else if (ev.type == "midi_note_off") {
            Operator* op = chain.getByName(ev.op);
            if (op) {
                float val[4] = {static_cast<float>(ev.note), 0, 0, 0};
                op->setParam("midi_note_off", val);
            }
        } else if (ev.type == "mouse_move") {
            float px = ev.x * static_cast<float>(ctx.width());
            float py = ev.y * static_cast<float>(ctx.height());
            ctx.injectMousePosition(px, py);
        }
    }

    // Interpolate active ramps
    for (auto it = m_activeRamps.begin(); it != m_activeRamps.end(); ) {
        if (frame >= it->endFrame) {
            // Ramp complete — set final value and remove
            Operator* op = chain.getByName(it->op);
            if (op) {
                float val[4] = {it->to, 0, 0, 0};
                op->setParam(it->param, val);
                op->markDirty();
            }
            it = m_activeRamps.erase(it);
        } else if (frame > it->startFrame) {
            // Interpolate
            float t = static_cast<float>(frame - it->startFrame) /
                      static_cast<float>(it->endFrame - it->startFrame);
            float v = it->from + t * (it->to - it->from);
            Operator* op = chain.getByName(it->op);
            if (op) {
                float val[4] = {v, 0, 0, 0};
                op->setParam(it->param, val);
                op->markDirty();
            }
            ++it;
        } else {
            ++it;
        }
    }
}

std::vector<std::string> EventInjector::validate(const Chain& chain) const {
    std::vector<std::string> warnings;
    // getByName() isn't const but validation is read-only
    auto& mutableChain = const_cast<Chain&>(chain);

    for (const auto& ev : m_script.events) {
        if (ev.op.empty()) continue;

        // Check if operator exists
        Operator* op = mutableChain.getByName(ev.op);
        if (!op) {
            warnings.push_back("Event at frame " + std::to_string(ev.frame) +
                               ": operator '" + ev.op + "' not found");
            continue;
        }

        // For param events, check if parameter exists
        if ((ev.type == "param_set" || ev.type == "param_ramp") && !ev.param.empty()) {
            float val[4] = {0};
            if (!op->getParam(ev.param, val)) {
                warnings.push_back("Event at frame " + std::to_string(ev.frame) +
                                   ": parameter '" + ev.param + "' not found on '" + ev.op + "'");
            }
        }
    }

    return warnings;
}

} // namespace vivid
