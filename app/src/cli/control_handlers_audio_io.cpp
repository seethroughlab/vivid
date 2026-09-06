#include "cli/control_handlers_internal.h"

#include "app/app.h"
#include "app/app_settings.h"
#include "audio/audio_device_manager.h"

// ADR-0032 Phase A (decision #1): audio OUTPUT device I/O — enumerate the available hardware outputs and
// select one at runtime. Distinct from control_handlers_audio_devices.cpp, which despite its name is about
// plugin instrument/effect pickers. Runs on the main thread (process_pending), which owns the session the
// live device swap (reopen) takes over while the callback is drained — same discipline as the WAV bounce.
namespace vivid {

static json device_status_json(const audio::DeviceStatus& st) {
    return { {"name", st.active_name}, {"sample_rate", st.actual_sample_rate},
             {"period", st.actual_period}, {"open", st.open},
             {"using_fallback", st.using_fallback}, {"reason", st.reason},
             // ADR-0032 Phase D1: the input (capture) side of a duplex device.
             {"input_open", st.input_open}, {"input_name", st.input_active_name},
             {"input_latency_frames", st.input_latency_frames} };
}

void register_audio_io_handlers(Handlers& handlers_) {
    // {} -> {ok, devices:[{name,is_default}], inputs:[{name,is_default}], active:{...incl. input_*}}
    handlers_["get_audio_devices"] = [](const ControlCtx& c, const json&) -> json {
        if (!c.app) return err(code::kInternal, "no app");
        json r = ok();
        json devs = json::array();
        json ins  = json::array();
        if (c.app->audio_devices) {
            for (const auto& d : c.app->audio_devices->enumerate())
                devs.push_back({ {"name", d.name}, {"is_default", d.is_default} });
            for (const auto& d : c.app->audio_devices->enumerate_inputs())
                ins.push_back({ {"name", d.name}, {"is_default", d.is_default} });
            r["active"] = device_status_json(c.app->audio_devices->status());
        } else {
            r["active"] = { {"open", false} };   // headless / audio unavailable
        }
        r["devices"] = devs;
        r["inputs"]  = ins;
        return r;
    };

    // {name} -> {ok, active:{...}}. Empty name selects the system default OUTPUT. Persists the choice
    // (machine-level settings.json) and hot-swaps the live device. device_prefs_from carries the input
    // prefs too, so switching output never drops an enabled input.
    handlers_["set_audio_device"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app) return err(code::kInternal, "no app");
        if (!c.app->audio_devices) return err(code::kInternal, "audio device unavailable");
        const std::string name = b.value("name", std::string());   // "" => system default

        // Load-modify-write: settings.json is authoritative on disk (reduce_motion writes it on every
        // toggle), so read it, update just the output name, and reopen with the full (in+out) prefs.
        const std::string path = app_settings_path();
        AppSettings s = load_app_settings(path);
        s.audio_device_name = name;

        audio::DevicePrefs p = device_prefs_from(s);
        p.sample_rate = 0;   // reopen() pins to the session rate internally
        if (!c.app->audio_devices->reopen(*c.app, p))
            return err(code::kBadArg, "failed to open audio device '" + name + "'");

        save_app_settings(s, path);
        const auto& st = c.app->audio_devices->status();
        if (st.using_fallback) VLOG_WARN(*c.app, "%s", st.reason.c_str());
        json r = ok();
        r["active"] = device_status_json(st);
        return r;
    };

    // {name, enabled} -> {ok, active:{...}}. ADR-0032 Phase D1: select/enable the hardware INPUT
    // (opens the device DUPLEX). enabled=false returns to playback-only; empty name = default input.
    // Input is best-effort — if capture can't open, the device stays playback-only (active.input_open
    // = false) but the call still succeeds (output is never sacrificed for input).
    handlers_["set_audio_input_device"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app) return err(code::kInternal, "no app");
        if (!c.app->audio_devices) return err(code::kInternal, "audio device unavailable");
        const std::string name = b.value("name", std::string());     // "" => system default input
        const bool enabled     = b.value("enabled", true);           // default: selecting => enable

        const std::string path = app_settings_path();
        AppSettings s = load_app_settings(path);
        s.audio_input_enabled = enabled;
        s.audio_input_name    = name;

        audio::DevicePrefs p = device_prefs_from(s);
        p.sample_rate = 0;   // reopen() pins to the session rate internally
        if (!c.app->audio_devices->reopen(*c.app, p))
            return err(code::kBadArg, "failed to reopen audio device");

        save_app_settings(s, path);
        const auto& st = c.app->audio_devices->status();
        if (enabled && !st.input_open) VLOG_WARN(*c.app, "%s",
            st.reason.empty() ? "audio input unavailable — output only" : st.reason.c_str());
        json r = ok();
        r["active"] = device_status_json(st);
        return r;
    };

    // ---------------- hardware MIDI input ----------------
    // Before this, the ONLY report of MIDI input state was a single fprintf at launch: an agent
    // could not tell whether a keyboard was attached, whether it was the right one, or whether it
    // was sending anything — so "my keyboard doesn't work" was undiagnosable over MCP.
    handlers_["midi_input_status"] = [](const ControlCtx& c, const json&) -> json {
        if (!c.app) return err(code::kInternal, "no app");
        const auto& mi = c.app->midi_in;
        json srcs = json::array();
        for (const auto& s : mi.sources())
            srcs.push_back({ {"id", static_cast<int>(s.id)}, {"name", s.name}, {"connected", s.connected} });
        json r = ok();
        r["sources"]   = srcs;
        r["connected"] = mi.source_count();
        r["selected_source"]  = static_cast<int>(mi.selected_source());   // 0 = any
        r["selected_channel"] = mi.selected_channel();                    // -1 = omni
        r["events_seen"] = mi.events_seen();
        r["receiving"]   = mi.events_seen() > 0;
        if (srcs.empty())
            r["hint"] = "no MIDI sources — plug the keyboard in (it is picked up live; no restart "
                        "needed) and check it is not claimed exclusively by another app";
        else if (mi.events_seen() == 0)
            r["hint"] = "sources are connected but nothing has arrived yet — play a note; if still "
                        "zero, check the keyboard's MIDI channel against selected_channel";
        return r;
    };

    handlers_["midi_input_select"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app) return err(code::kInternal, "no app");
        const int32_t source  = static_cast<int32_t>(b.value("source", 0));   // 0 = every source
        const int     channel = b.value("channel", -1);                       // -1 = omni
        if (channel < -1 || channel > 15) return err(code::kBadArg, "channel must be -1 (omni) or 0..15");
        if (source != 0) {   // reject an id that is not actually present, rather than going deaf
            bool found = false;
            for (const auto& s : c.app->midi_in.sources()) if (s.id == source) { found = true; break; }
            if (!found) return err(code::kNotFound, "no MIDI source with id " + std::to_string(source) +
                                                    " (see midi_input_status)");
        }
        c.app->midi_in.select(source, channel);

        // Machine-level, like the audio device prefs — not part of the project, so not undoable.
        const std::string path = app_settings_path();
        AppSettings s = load_app_settings(path);
        s.midi_input_source  = source;
        s.midi_input_channel = channel;
        save_app_settings(s, path);

        json r = ok();
        r["selected_source"]  = static_cast<int>(source);
        r["selected_channel"] = channel;
        r["connected"]        = c.app->midi_in.source_count();
        return r;
    };
}

}  // namespace vivid
