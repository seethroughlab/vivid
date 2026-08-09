#include "audio/audio_device_manager.h"
#include "audio/audio_callback.h"   // audio_callback (the RT data callback)
#include "miniaudio.h"

#include <string>
#include <vector>

namespace vivid::audio {

// resolve_device_index is defined inline in the header (pure, no miniaudio).
// miniaudio.h is confined to this .cpp via the PIMPL.
struct AudioDeviceManager::Impl {
    ma_context context{};
    ma_device  device{};
    bool       context_ok = false;
    bool       device_open = false;
};

AudioDeviceManager::AudioDeviceManager() : impl_(std::make_unique<Impl>()) {
    impl_->context_ok = (ma_context_init(nullptr, 0, nullptr, &impl_->context) == MA_SUCCESS);
}

AudioDeviceManager::~AudioDeviceManager() {
    close();
    if (impl_->context_ok) ma_context_uninit(&impl_->context);
}

std::vector<DeviceInfo> AudioDeviceManager::enumerate() {
    std::vector<DeviceInfo> out;
    if (!impl_->context_ok) return out;
    ma_device_info* playback = nullptr; ma_uint32 pcount = 0;
    ma_device_info* capture  = nullptr; ma_uint32 ccount = 0;
    if (ma_context_get_devices(&impl_->context, &playback, &pcount, &capture, &ccount) != MA_SUCCESS)
        return out;
    for (ma_uint32 i = 0; i < pcount; ++i)
        out.push_back({ playback[i].name, playback[i].isDefault != 0 });
    return out;
}

std::vector<DeviceInfo> AudioDeviceManager::enumerate_inputs() {
    std::vector<DeviceInfo> out;
    if (!impl_->context_ok) return out;
    ma_device_info* playback = nullptr; ma_uint32 pcount = 0;
    ma_device_info* capture  = nullptr; ma_uint32 ccount = 0;
    if (ma_context_get_devices(&impl_->context, &playback, &pcount, &capture, &ccount) != MA_SUCCESS)
        return out;
    for (ma_uint32 i = 0; i < ccount; ++i)
        out.push_back({ capture[i].name, capture[i].isDefault != 0 });
    return out;
}

bool AudioDeviceManager::open(App& app, const DevicePrefs& p) {
    status_ = DeviceStatus{};
    close();   // drop any prior device
    if (!impl_->context_ok) { status_.reason = "audio context unavailable"; return false; }

    // Enumerate to (a) build the resolvable name lists and (b) keep the raw ma_device_info arrays whose
    // .id we open by. The lists run in the same order as the arrays, so resolve_device_index indexes both.
    ma_device_info* playback = nullptr; ma_uint32 pcount = 0;
    ma_device_info* capture  = nullptr; ma_uint32 ccount = 0;
    std::vector<DeviceInfo> devices;   // outputs
    std::vector<DeviceInfo> inputs;    // capture devices
    if (ma_context_get_devices(&impl_->context, &playback, &pcount, &capture, &ccount) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < pcount; ++i)
            devices.push_back({ playback[i].name, playback[i].isDefault != 0 });
        for (ma_uint32 i = 0; i < ccount; ++i)
            inputs.push_back({ capture[i].name, capture[i].isDefault != 0 });
    }

    int idx = resolve_device_index(devices, p.requested_name);
    const bool wanted_specific = !p.requested_name.empty();
    if (idx < 0 && wanted_specific) {
        if (!p.fallback_to_default) {
            status_.reason = "requested audio output '" + p.requested_name + "' not found";
            return false;
        }
        status_.using_fallback = true;
        status_.reason = "saved audio output '" + p.requested_name + "' not found — using default";
    }

    // ADR-0032 Phase D1: resolve the requested INPUT (only when enabled). A missing specific input
    // resolves to -1 = the system default input (a soft fallback — never a hard failure; input is a
    // bonus). ccount==0 (no capture hardware) forces playback-only regardless of the pref.
    const bool want_input = p.enable_input && ccount > 0;
    const int  in_idx = want_input ? resolve_device_index(inputs, p.input_name) : -1;

    // Build the device config. `with_input` selects a DUPLEX device (capture + playback share one clock
    // and one callback); otherwise a pure playback device (today's behavior). `out_id`/`in_id` are the
    // resolved ma_device_id* (null => the system default for that direction).
    auto build_cfg = [&](bool with_input, const ma_device_id* out_id, const ma_device_id* in_id) {
        ma_device_config cfg = ma_device_config_init(with_input ? ma_device_type_duplex
                                                                 : ma_device_type_playback);
        cfg.playback.format    = ma_format_f32;
        cfg.playback.channels  = 2;
        cfg.sampleRate         = p.sample_rate;    // 0 => device native rate (unchanged launch behavior)
        cfg.dataCallback       = audio_callback;
        cfg.pUserData          = &app;             // the RT callback casts this back to App*
        cfg.periodSizeInFrames = p.period_frames;
        cfg.performanceProfile = ma_performance_profile_conservative;
        cfg.playback.pDeviceID = out_id;
        if (with_input) {
            cfg.capture.format   = ma_format_f32;
            cfg.capture.channels = 2;
            cfg.capture.pDeviceID = in_id;
        }
        return cfg;
    };

    const ma_device_id* out_id = (idx    >= 0) ? &playback[idx].id   : nullptr;
    const ma_device_id* in_id  = (in_idx >= 0) ? &capture[in_idx].id : nullptr;

    // Open with a small retry ladder, most-preferred first. Output must always succeed; input is
    // best-effort (drops to playback-only on any duplex failure). `opened_with_input` records which rung
    // won so status/reason reflect reality. `default_out` is set when the specific output fell back.
    bool opened_with_input = false;
    bool default_out       = false;
    ma_result rc = MA_ERROR;
    struct Rung { bool with_input; bool def_out; };
    std::vector<Rung> ladder;
    ladder.push_back({ want_input, false });                          // 1. as requested
    if (want_input)                          ladder.push_back({ false, false });   // 2. drop input
    if (idx >= 0 && p.fallback_to_default)   ladder.push_back({ want_input, true });  // 3. default output
    if (idx >= 0 && p.fallback_to_default && want_input)
                                             ladder.push_back({ false, true });   // 4. drop both
    for (const Rung& r : ladder) {
        ma_device_config cfg = build_cfg(r.with_input, r.def_out ? nullptr : out_id, in_id);
        rc = ma_device_init(&impl_->context, &cfg, &impl_->device);
        if (rc == MA_SUCCESS) { opened_with_input = r.with_input; default_out = r.def_out; break; }
    }
    if (rc != MA_SUCCESS) { status_.reason = "no audio output device available"; return false; }

    // Reconcile status/reason with the winning rung.
    if (default_out) {
        status_.using_fallback = true;
        status_.reason = "audio output '" + devices[static_cast<size_t>(idx)].name +
                         "' failed to open — using default";
        idx = -1;   // now on the system default output
    }
    if (want_input && !opened_with_input) {
        // Preserve any output-fallback reason set just above rather than clobbering it (both can happen).
        status_.reason = status_.reason.empty() ? "audio input unavailable — output only"
                                                : status_.reason + "; audio input unavailable";
    }

    impl_->device_open = true;
    status_.open = true;
    status_.actual_sample_rate = impl_->device.sampleRate;
    status_.actual_period = p.period_frames;
    // ADR-0032 Phase B: the backend's internal output buffering (only valid once opened). miniaudio has
    // no accessor — compute from the negotiated internal period × count.
    status_.output_latency_frames =
        impl_->device.playback.internalPeriodSizeInFrames * impl_->device.playback.internalPeriods;
    if (idx >= 0) {
        status_.active_name = devices[static_cast<size_t>(idx)].name;
    } else {
        // Opened the system default — surface its name (if enumeration knows it) for the UI.
        for (const auto& d : devices) if (d.is_default) { status_.active_name = d.name; break; }
    }
    // ADR-0032 Phase D1: the input side of a duplex open.
    status_.input_open = opened_with_input;
    if (opened_with_input) {
        status_.input_latency_frames =
            impl_->device.capture.internalPeriodSizeInFrames * impl_->device.capture.internalPeriods;
        if (in_idx >= 0) status_.input_active_name = inputs[static_cast<size_t>(in_idx)].name;
        else for (const auto& d : inputs) if (d.is_default) { status_.input_active_name = d.name; break; }
    }
    if (session_rate_ == 0) session_rate_ = impl_->device.sampleRate;   // pin the session rate on first open
    return true;
}

bool AudioDeviceManager::reopen(App& app, DevicePrefs p) {
    if (session_rate_ != 0) p.sample_rate = session_rate_;   // keep the callback at the pinned session rate
    stop();                                                  // blocks until the in-flight callback returns
    if (!open(app, p)) return false;                         // close()s the old device, inits + resolves the new
    return start();
    // The worker-pool workgroup is intentionally NOT re-handed here — session_set_audio_workgroup is
    // start-once (see audio_device_manager.h). The launch device's workgroup persists.
}

bool AudioDeviceManager::start() {
    return impl_->device_open && ma_device_start(&impl_->device) == MA_SUCCESS;
}
void AudioDeviceManager::stop()  { if (impl_->device_open) ma_device_stop(&impl_->device); }

void AudioDeviceManager::close() {
    if (impl_->device_open) { ma_device_uninit(&impl_->device); impl_->device_open = false; }
    status_.open = false;
}

void* AudioDeviceManager::device_handle() { return impl_->device_open ? &impl_->device : nullptr; }

}  // namespace vivid::audio
