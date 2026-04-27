#include "runtime/audio/audio_device_list.h"

#include <miniaudio.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace vivid {

struct AudioDeviceList::Snapshot {
    std::vector<std::string>  names;        // index 0 = "Default"
    std::vector<const char*>  name_ptrs;    // points into `names`
    std::vector<ma_device_id> ids;          // index 0 is a zeroed sentinel (unused)
    std::vector<uint8_t>      has_id;       // 0 for index 0 (Default), 1 for enumerated devices
    std::vector<uint32_t>     preferred_rates; // 0 if unknown
};

namespace {

// Pick the best native sample rate from a device's nativeDataFormats.
// Prefers ma_format_f32; among rates, picks the highest <= 96000.
// Returns 0 if no usable entry was found.
uint32_t pick_preferred_rate(const ma_device_info& info) {
    uint32_t best = 0;
    for (ma_uint32 i = 0; i < info.nativeDataFormatCount; ++i) {
        const auto& f = info.nativeDataFormats[i];
        if (f.sampleRate == 0) continue;
        if (f.sampleRate > 96000) continue;
        // Prefer f32 entries; if we already have a non-f32 best, an f32 of
        // any rate <= 96000 wins; otherwise pick the higher rate.
        bool prefer = (f.format == ma_format_f32) ||
                      (best == 0) ||
                      (f.sampleRate > best);
        if (prefer) best = f.sampleRate;
    }
    return best;
}

} // namespace

AudioDeviceList& AudioDeviceList::instance() {
    static AudioDeviceList g;
    return g;
}

AudioDeviceList::~AudioDeviceList() = default;

bool AudioDeviceList::refresh() {
    auto snap = std::make_shared<Snapshot>();

    // Index 0 is always "Default" (system default; nullptr id).
    snap->names.emplace_back("Default");
    snap->ids.emplace_back();             // zeroed sentinel; never read
    snap->has_id.push_back(0);
    snap->preferred_rates.push_back(0);   // filled in below if context init succeeds

    ma_context ctx;
    ma_result res = ma_context_init(nullptr, 0, nullptr, &ctx);
    if (res == MA_SUCCESS) {
        // Default device's preferred rate.
        ma_device_info default_info;
        if (ma_context_get_device_info(&ctx, ma_device_type_playback, nullptr,
                                       &default_info) == MA_SUCCESS) {
            snap->preferred_rates[0] = pick_preferred_rate(default_info);
        }

        ma_device_info* playback_infos = nullptr;
        ma_uint32 playback_count = 0;
        res = ma_context_get_devices(&ctx, &playback_infos, &playback_count,
                                     nullptr, nullptr);
        if (res == MA_SUCCESS) {
            snap->names.reserve(snap->names.size() + playback_count);
            snap->ids.reserve(snap->ids.size() + playback_count);
            snap->has_id.reserve(snap->has_id.size() + playback_count);
            snap->preferred_rates.reserve(snap->preferred_rates.size() + playback_count);
            for (ma_uint32 i = 0; i < playback_count; ++i) {
                snap->names.emplace_back(playback_infos[i].name);
                snap->ids.push_back(playback_infos[i].id);
                snap->has_id.push_back(1);

                ma_device_info info;
                uint32_t rate = 0;
                if (ma_context_get_device_info(&ctx, ma_device_type_playback,
                                               &playback_infos[i].id,
                                               &info) == MA_SUCCESS) {
                    rate = pick_preferred_rate(info);
                }
                snap->preferred_rates.push_back(rate);
            }
        } else {
            std::fprintf(stderr, "[vivid] AudioDeviceList: ma_context_get_devices failed: %d\n", res);
        }
        ma_context_uninit(&ctx);
    } else {
        std::fprintf(stderr, "[vivid] AudioDeviceList: ma_context_init failed: %d\n", res);
    }

    // Build label-pointer array now that `names` is final (vector growth
    // may have invalidated earlier c_str() pointers).
    snap->name_ptrs.reserve(snap->names.size());
    for (const auto& n : snap->names) snap->name_ptrs.push_back(n.c_str());

    // Diff against the previous active snapshot. We compare names + ids +
    // preferred_rates; if any differ, this is a real change.
    bool changed = false;
    if (!active_) {
        changed = true;
    } else {
        const auto& prev = *active_;
        if (prev.names.size() != snap->names.size()) {
            changed = true;
        } else {
            for (size_t i = 0; i < snap->names.size(); ++i) {
                if (prev.names[i] != snap->names[i]) { changed = true; break; }
                if (prev.has_id[i] != snap->has_id[i]) { changed = true; break; }
                if (snap->has_id[i] &&
                    std::memcmp(&prev.ids[i], &snap->ids[i], sizeof(ma_device_id)) != 0) {
                    changed = true; break;
                }
                if (prev.preferred_rates[i] != snap->preferred_rates[i]) {
                    changed = true; break;
                }
            }
        }
    }

    if (changed) {
        active_ = std::move(snap);
    } else if (!active_) {
        // First call with empty list — promote it.
        active_ = std::move(snap);
    }
    // If unchanged, keep the previous active_ (which descriptor_pin_ may
    // already reference) so existing pointers remain stable.
    return changed;
}

uint32_t AudioDeviceList::count() const {
    return active_ ? static_cast<uint32_t>(active_->name_ptrs.size()) : 0;
}

const char** AudioDeviceList::label_ptrs() const {
    if (!active_ || active_->name_ptrs.empty()) return nullptr;
    return const_cast<const char**>(active_->name_ptrs.data());
}

const void* AudioDeviceList::device_id_for_index(uint32_t i) const {
    if (!active_) return nullptr;
    if (i >= active_->ids.size()) return nullptr;
    if (!active_->has_id[i]) return nullptr;   // index 0 = Default
    return &active_->ids[i];
}

uint32_t AudioDeviceList::preferred_rate_for_index(uint32_t i) const {
    if (!active_) return 0;
    if (i >= active_->preferred_rates.size()) return 0;
    return active_->preferred_rates[i];
}

size_t AudioDeviceList::device_id_bytes() {
    return sizeof(ma_device_id);
}

int AudioDeviceList::find_index_by_id_bytes(const void* bytes, size_t len) const {
    if (!active_) return -1;
    if (!bytes || len == 0) return -1;
    if (len != sizeof(ma_device_id)) return -1;
    for (size_t i = 0; i < active_->ids.size(); ++i) {
        if (!active_->has_id[i]) continue;
        if (std::memcmp(&active_->ids[i], bytes, sizeof(ma_device_id)) == 0)
            return static_cast<int>(i);
    }
    return -1;
}

void AudioDeviceList::pin_active_for_descriptor() {
    descriptor_pin_ = active_;
}

} // namespace vivid
