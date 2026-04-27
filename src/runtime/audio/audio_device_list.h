#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace vivid {

// ---------------------------------------------------------------------------
// AudioDeviceList — process-wide enumeration of system playback devices.
//
// Single source of truth shared by the `audio_out` operator descriptor (for
// the dropdown labels) and AudioExecutor (for the ma_device_id pointer passed
// to ma_device_init). Names and ids stay aligned by index.
//
// Index 0 is always "Default" and resolves to nullptr (i.e. miniaudio's
// system-default device). Indices 1..count()-1 are enumerated playback
// devices in the order miniaudio reports them.
//
// All methods are intended to be called from the main thread. Storage is
// snapshot-and-swap so that pointers handed out (via `label_ptrs()` /
// `device_id_for_index()`) stay valid until the next refresh+sync cycle:
// callers who pin a snapshot via `pin_active_for_descriptor()` keep it alive
// for as long as the cached pointer is referenced.
// ---------------------------------------------------------------------------
class AudioDeviceList {
public:
    static AudioDeviceList& instance();

    // (Re-)enumerate playback devices via a temporary ma_context. Returns
    // true if the new snapshot differs from the previous one (in names, ids,
    // or preferred sample rates). When false, the previous snapshot is
    // retained and pointers handed out earlier remain valid.
    bool refresh();

    uint32_t count() const;

    // VividParamDescriptor::choice_labels expects `const char**`.
    const char** label_ptrs() const;

    // Returns the ma_device_id pointer for a given index, suitable for
    // assignment to ma_device_config::playback.pDeviceID after casting to
    // `const ma_device_id*`. `void*` to keep <miniaudio.h> out of this header.
    // Index 0 ("Default") returns nullptr. Out-of-range returns nullptr.
    const void* device_id_for_index(uint32_t i) const;

    // System-preferred sample rate for the device at this index, in Hz.
    // 0 if unknown (callers should fall back to 48000).
    uint32_t preferred_rate_for_index(uint32_t i) const;

    // Size in bytes of the underlying ma_device_id type. Callers that want
    // to identify a device across snapshots can copy that many bytes from
    // the pointer returned by `device_id_for_index()` and later pass them
    // back to `find_index_by_id_bytes()`.
    static size_t device_id_bytes();

    // Returns the index of the device whose id matches the given byte
    // buffer in the current snapshot, or -1 if no match. Used by
    // AudioExecutor to detect when the currently-open device has been
    // unplugged after a refresh.
    int find_index_by_id_bytes(const void* bytes, size_t len) const;

    // Pin the current snapshot so its name/id pointers remain valid even if
    // a later refresh swaps in a new snapshot. Call this after copying
    // pointers into the audio_out param descriptor.
    void pin_active_for_descriptor();

private:
    AudioDeviceList() = default;
    ~AudioDeviceList();
    AudioDeviceList(const AudioDeviceList&) = delete;
    AudioDeviceList& operator=(const AudioDeviceList&) = delete;

    struct Snapshot;
    std::shared_ptr<const Snapshot> active_;
    std::shared_ptr<const Snapshot> descriptor_pin_;
};

} // namespace vivid
