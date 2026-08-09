// Headless test for the ADR-0032 Phase A device-name resolution — the pure seam of AudioDeviceManager
// (audio_device_manager.h has no miniaudio.h, so this links in the deps-free suite). resolve_device_index
// maps a persisted device NAME to an index in the freshly-enumerated list, or -1 = "open the system
// default" (empty request, or a saved device that is gone). First match wins on duplicate names.
#include "audio/audio_device_manager.h"
#include "test_helpers.h"

#include <string>
#include <vector>

using vivid::audio::DeviceInfo;
using vivid::audio::DevicePrefs;
using vivid::audio::DeviceStatus;
using vivid::audio::resolve_device_index;

int main() {
    const std::vector<DeviceInfo> devices = {
        { "Built-in Output", true },
        { "External Headphones", false },
        { "USB Interface", false },
    };

    // Empty request => follow the system default (-1), never a named index.
    CHECK(resolve_device_index(devices, "") == -1);
    // A present device resolves to its index.
    CHECK(resolve_device_index(devices, "Built-in Output") == 0);
    CHECK(resolve_device_index(devices, "External Headphones") == 1);
    CHECK(resolve_device_index(devices, "USB Interface") == 2);
    // A saved device that is gone => -1 (the caller falls back to default).
    CHECK(resolve_device_index(devices, "Thunderbolt Dock") == -1);
    // Case-sensitive exact match (no fuzzy match).
    CHECK(resolve_device_index(devices, "built-in output") == -1);
    // Empty device list (no hardware / headless) => -1 for any request.
    CHECK(resolve_device_index({}, "Built-in Output") == -1);
    CHECK(resolve_device_index({}, "") == -1);

    // First match wins on duplicate names (identical interface names collide — documented v1 limitation).
    const std::vector<DeviceInfo> dupes = { { "Aggregate", false }, { "Aggregate", false } };
    CHECK(resolve_device_index(dupes, "Aggregate") == 0);

    // Struct defaults: native rate (0), pinned period, fallback on.
    DevicePrefs p;
    CHECK(p.requested_name.empty());
    CHECK(p.sample_rate == 0u);
    CHECK(p.period_frames == 1024u);
    CHECK(p.fallback_to_default == true);

    DeviceStatus st;
    CHECK(st.open == false);
    CHECK(st.using_fallback == false);
    CHECK(st.actual_sample_rate == 0u);

    return vivid::test::summary("test_audio_device_resolve");
}
