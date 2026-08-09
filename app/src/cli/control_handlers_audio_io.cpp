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
             {"using_fallback", st.using_fallback}, {"reason", st.reason} };
}

void register_audio_io_handlers(Handlers& handlers_) {
    // {} -> {ok, devices:[{name,is_default}], active:{name,sample_rate,period,open,using_fallback,reason}}
    handlers_["get_audio_devices"] = [](const ControlCtx& c, const json&) -> json {
        if (!c.app) return err(code::kInternal, "no app");
        json r = ok();
        json devs = json::array();
        if (c.app->audio_devices) {
            for (const auto& d : c.app->audio_devices->enumerate())
                devs.push_back({ {"name", d.name}, {"is_default", d.is_default} });
            r["active"] = device_status_json(c.app->audio_devices->status());
        } else {
            r["active"] = { {"open", false} };   // headless / audio unavailable
        }
        r["devices"] = devs;
        return r;
    };

    // {name} -> {ok, active:{...}}. Empty name selects the system default. Persists the choice
    // (machine-level settings.json) and hot-swaps the live device.
    handlers_["set_audio_device"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app) return err(code::kInternal, "no app");
        if (!c.app->audio_devices) return err(code::kInternal, "audio device unavailable");
        const std::string name = b.value("name", std::string());   // "" => system default

        // Load-modify-write: the device prefs share settings.json with reduce_motion, and reduce_motion
        // is written to disk on every toggle, so disk is authoritative — updating one field here never
        // clobbers the other.
        const std::string path = app_settings_path();
        AppSettings s = load_app_settings(path);
        s.audio_device_name = name;

        audio::DevicePrefs p;
        p.requested_name      = name;
        p.sample_rate         = 0;   // reopen() pins to the session rate internally
        p.period_frames       = s.audio_period_frames;
        p.fallback_to_default = s.audio_fallback_to_default;
        if (!c.app->audio_devices->reopen(*c.app, p))
            return err(code::kBadArg, "failed to open audio device '" + name + "'");

        save_app_settings(s, path);
        const auto& st = c.app->audio_devices->status();
        if (st.using_fallback) VLOG_WARN(*c.app, "%s", st.reason.c_str());
        json r = ok();
        r["active"] = device_status_json(st);
        return r;
    };
}

}  // namespace vivid
