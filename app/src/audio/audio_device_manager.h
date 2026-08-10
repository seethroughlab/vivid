#pragma once
// ADR-0032 Phase A — the audio output device model.
//
// Owns the miniaudio playback context + device behind a PIMPL so miniaudio.h stays out of this header
// (main.cpp, the control handlers, the diagnostics panel, and the headless resolve test all include it).
// Replaces the bare `ma_device` main.cpp local: enumerate available outputs, open a chosen-or-default
// device, and (later slices) hot-swap it at runtime. The opened `ma_device` lives INSIDE the manager's
// Impl, so its address is stable across a future reopen — App::audio_device (the opaque handle the
// offline bounce pauses/resumes) is set once and never has to move.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vivid { struct App; }

namespace vivid::audio {

// One enumerated output device. `name` is the stable identity we persist (miniaudio's device id is a
// backend-specific union that does not serialize; the name is the only field guaranteed filled during
// enumeration).
struct DeviceInfo {
    std::string name;
    bool        is_default = false;
};

// Machine-level preferences (persisted in AppSettings → settings.json), NOT per-project.
struct DevicePrefs {
    std::string requested_name;          // "" => follow the system default OUTPUT device
    uint32_t    sample_rate = 0;         // 0 => open at the device's native rate (today's behavior)
    uint32_t    period_frames = 1024;    // requested buffer size (== kDefaultDevicePeriod)
    bool        fallback_to_default = true;
    // ADR-0032 Phase D1 — hardware audio INPUT (capture). Off by default => a pure playback device,
    // identical to today. When enabled, the device opens DUPLEX (one clock, one callback) so `in` is
    // live in the same callback as `out`. Input is best-effort: if the duplex open fails the device
    // falls back to playback-only so output always works (see DeviceStatus.input_open).
    bool        enable_input = false;
    std::string input_name;              // "" => system default INPUT device (only used when enable_input)
};

// How the last open resolved — feeds the health/log surfacing + the diagnostics "Audio Device" row.
struct DeviceStatus {
    bool        open = false;
    std::string active_name;             // the device actually opened ("" => system default)
    uint32_t    actual_sample_rate = 0;  // device.sampleRate after open — the rate the callback runs at
    uint32_t    actual_period = 0;       // requested period echoed back
    // ADR-0032 Phase B: honest output latency = the backend's internal buffering in frames
    // (internalPeriodSizeInFrames × internalPeriods). 0 when unknown/headless. ms = frames·1000/sr.
    uint32_t    output_latency_frames = 0;
    bool        using_fallback = false;  // the requested device was unavailable → opened default instead
    std::string reason;                  // human-readable fallback/failure text (VLOG + panel)
    // ADR-0032 Phase D1 — the input (capture) side of a duplex device. input_open is false whenever the
    // device is playback-only (input disabled, or a duplex open that fell back to output-only). The
    // capture latency is the backend's internal input buffering (0 when unknown/none).
    bool        input_open = false;
    std::string input_active_name;       // the input device actually opened ("" => system default)
    uint32_t    input_latency_frames = 0;
};

// PURE, headless-testable resolution: the index into `devices` whose name equals `requested_name`, or
// -1 to mean "open the system default" (empty request, or a request not present). First match wins on
// duplicate names. Defined inline (no miniaudio) so the resolve test links without the .cpp.
inline int resolve_device_index(const std::vector<DeviceInfo>& devices, const std::string& requested_name) {
    if (requested_name.empty()) return -1;                 // follow the system default
    for (size_t i = 0; i < devices.size(); ++i)
        if (devices[i].name == requested_name) return static_cast<int>(i);   // first match wins
    return -1;                                             // requested device is gone
}

class AudioDeviceManager {
public:
    AudioDeviceManager();               // ma_context_init (default backend); tolerates failure
    ~AudioDeviceManager();              // close() + ma_context_uninit

    AudioDeviceManager(const AudioDeviceManager&) = delete;
    AudioDeviceManager& operator=(const AudioDeviceManager&) = delete;

    // Available playback outputs. Empty (never throws/crashes) when the context failed to init or the
    // machine has no output device — the caller then runs headless exactly as before this class existed.
    std::vector<DeviceInfo> enumerate();

    // ADR-0032 Phase D1 — available capture INPUTS (mic / line-in / interface). Same tolerance as
    // enumerate(): empty when the context failed or the machine has no input. resolve_device_index works
    // on this list unchanged (name → index, -1 => system default).
    std::vector<DeviceInfo> enumerate_inputs();

    // Resolve `p` against the current devices and open it (or the default on miss/failure, when
    // p.fallback_to_default). Sets the callback + pUserData=&app. Returns true iff a device is open.
    // Fills status(). Does NOT start the device (call start() after the session is built).
    bool open(App& app, const DevicePrefs& p);

    // Live hot-swap (main thread): stop → open(requested name, at the PINNED session rate) → start.
    // Requesting the pinned rate (not the device native rate) keeps the callback at the session rate, so
    // the session engine is never rebuilt. On failure, open()'s fallback-to-default applies. Returns true
    // iff a device is running afterward. NOTE: the ADR-0052 worker-pool os_workgroup is start-once, so a
    // swapped device keeps the LAUNCH device's workgroup — RT scheduling on the new device is slightly
    // degraded but functional (re-handoff needs a pool restart; a follow-up).
    bool reopen(App& app, DevicePrefs p);

    bool start();                       // ma_device_start; false if not open or start failed
    void stop();                        // ma_device_stop  (blocks until the in-flight callback returns)
    void close();                       // stop + uninit the device (idempotent)

    const DeviceStatus& status() const { return status_; }
    // The opened ma_device* as an opaque handle — stable across the manager's lifetime. Null if closed.
    void* device_handle();
    // The pinned session sample rate (the first successful open's readback). 0 until opened.
    uint32_t session_sample_rate() const { return session_rate_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    DeviceStatus status_;
    uint32_t     session_rate_ = 0;
};

}  // namespace vivid::audio
