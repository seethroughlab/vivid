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

bool AudioDeviceManager::open(App& app, const DevicePrefs& p) {
    status_ = DeviceStatus{};
    close();   // drop any prior device
    if (!impl_->context_ok) { status_.reason = "audio context unavailable"; return false; }

    // Enumerate to (a) build the resolvable name list and (b) keep the raw ma_device_info array whose
    // .id we open by. The two run in the same order, so resolve_device_index's result indexes both.
    ma_device_info* playback = nullptr; ma_uint32 pcount = 0;
    ma_device_info* capture  = nullptr; ma_uint32 ccount = 0;
    std::vector<DeviceInfo> devices;
    if (ma_context_get_devices(&impl_->context, &playback, &pcount, &capture, &ccount) == MA_SUCCESS)
        for (ma_uint32 i = 0; i < pcount; ++i)
            devices.push_back({ playback[i].name, playback[i].isDefault != 0 });

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

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format     = ma_format_f32;
    cfg.playback.channels   = 2;
    cfg.sampleRate          = p.sample_rate;      // 0 => device native rate (unchanged launch behavior)
    cfg.dataCallback        = audio_callback;
    cfg.pUserData           = &app;               // the RT callback casts this back to App*
    cfg.periodSizeInFrames  = p.period_frames;
    cfg.performanceProfile  = ma_performance_profile_conservative;
    if (idx >= 0) cfg.playback.pDeviceID = &playback[idx].id;

    if (ma_device_init(&impl_->context, &cfg, &impl_->device) != MA_SUCCESS) {
        // A specific device that refused to open falls back to the system default (if allowed).
        if (idx >= 0 && p.fallback_to_default) {
            cfg.playback.pDeviceID = nullptr;
            status_.using_fallback = true;
            status_.reason = "audio output '" + devices[static_cast<size_t>(idx)].name +
                             "' failed to open — using default";
            idx = -1;
            if (ma_device_init(&impl_->context, &cfg, &impl_->device) != MA_SUCCESS) {
                status_.reason = "no audio output device available";
                return false;
            }
        } else {
            status_.reason = "no audio output device available";
            return false;
        }
    }

    impl_->device_open = true;
    status_.open = true;
    status_.actual_sample_rate = impl_->device.sampleRate;
    status_.actual_period = p.period_frames;
    if (idx >= 0) {
        status_.active_name = devices[static_cast<size_t>(idx)].name;
    } else {
        // Opened the system default — surface its name (if enumeration knows it) for the UI.
        for (const auto& d : devices) if (d.is_default) { status_.active_name = d.name; break; }
    }
    if (session_rate_ == 0) session_rate_ = impl_->device.sampleRate;   // pin the session rate on first open
    return true;
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
