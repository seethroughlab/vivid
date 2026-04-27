#pragma once

namespace vivid {
class OperatorRegistry;
}

void register_builtin_operators(vivid::OperatorRegistry& registry);

namespace vivid {
// Re-patch the audio_out param descriptor's device-choice fields from the
// current AudioDeviceList snapshot. Call after AudioDeviceList::refresh()
// reports a change so the dropdown reflects the new list. The function also
// pins the snapshot inside AudioDeviceList so the patched pointers stay
// valid until the next sync.
void sync_audio_out_device_choices();
}
