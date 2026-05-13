# AU Plugin Hosting

## Context

AU (Audio Units) hosting brings the macOS commercial plugin catalog into Vivid. Every major macOS plugin — instruments, effects, processors — ships AU. The format is part of the macOS SDK (`AudioToolbox`, `CoreAudio`), so no new vendored dependencies are required.

The work is structurally parallel to the existing CLAP host. The hard problems — operator pattern, triple-buffer plugin slot management, GUI window embedding, transport sync, state persistence, MCP tools — are already solved infrastructure. Target is **AU v2** (the C API that covers virtually all commercial plugins). AU v3 (the Swift/ObjC extension model) is a stretch goal.

## Prior Research

The CLAP hosting implementation at `operators/shared/clap_host/` and `operators/audio/clap_instrument/` is the direct reference for this work. Read it before starting — every structural decision here mirrors it.

## Operators to Build

### `au_instrument`
- **Domain:** Audio
- **Inputs:** `notes_in` (same note-event port used by all Vivid synths and `clap_instrument`)
- **Outputs:** `audio_float` (stereo)
- **Params:** `plugin_name` (string, display name of selected AU), `plugin_state` (hidden TEXT, base64-encoded `CFPropertyList`), `macro_0..7` + `macro_0_id..7_id` (same 8-slot macro system as CLAP)

### `au_effect`
- **Domain:** Audio
- **Inputs:** `audio_float` (stereo)
- **Outputs:** `audio_float` (stereo)
- **Params:** Same as above; passthrough when no plugin loaded

## Architecture

```
operators/shared/au_host/
├── au_host_common.h/.mm    AU instance wrapper, MIDI dispatch, host callbacks, state I/O
├── au_scanner.h/.mm        AudioComponentFindNext() enumeration + scan cache
├── au_browser.h            Plugin picker UI (same draw pattern as clap_browser.h)
└── au_plugin_window.h/.mm  NSView hosting in NSWindow (same as clap_plugin_window.mm)

operators/audio/
├── au_instrument/          AudioProcessable operator
└── au_effect/              AudioProcessable operator
```

The per-operator plugin slot uses the same **triple-buffered atomic** design as `CLAPInstrument`:
- `active_` — currently rendering on audio thread
- `pending_` — loaded and activated on main thread, waiting to swap in
- `dying_` — stopped by audio thread, awaiting `AudioUnitUninitialize()` on main thread

## Key AU v2 API Mappings

| Concern | CLAP | AU v2 |
|---------|------|-------|
| Discovery | `dlopen` + filesystem walk | `AudioComponentFindNext()` — Core Audio registry, no file scan |
| Instantiation | `factory->create_plugin()` | `AudioComponentInstanceNew(component, &au)` |
| Audio render | `plugin->process()` with `clap_process_t` | `AudioUnitRender(au, &flags, &ts, bus, frames, &buflist)` |
| MIDI notes | `clap_event_note_t` pushed to event list | `MusicDeviceMIDIEvent(au, status, d1, d2, offset)` — channel MIDI |
| Parameters | `clap_id` + `clap_plugin_params_t` | `AudioUnitParameterID` + `AudioUnitGetParameterInfo()` |
| Transport | `clap_event_transport_t` pushed each block | `kAudioUnitProperty_HostCallbacks` — plugin pulls via function pointers set at init |
| State save | `clap_plugin_state_t` streams | `AudioUnitGetProperty(kAudioUnitProperty_ClassInfo)` → `CFPropertyList` |
| State load | `clap_plugin_state_t::load()` stream | `AudioUnitSetProperty(kAudioUnitProperty_ClassInfo, ...)` |
| GUI | `clap_plugin_gui_t::set_parent()` → `NSView` | `kAudioUnitProperty_CocoaUI` → `NSView*` from named factory class |
| Thread model | Explicit per-function annotations | **None** — only `AudioUnitRender()` and `MusicDeviceMIDIEvent()` are audio-thread safe |

### Transport Host Callbacks

AU plugins request tempo and position by calling function pointers set via `kAudioUnitProperty_HostCallbacks`:

```c
HostCallbackInfo cbi = {};
cbi.hostUserData        = this;
cbi.beatAndTempoProc    = [](void* ud, Float64* beat, Float64* bpm) -> OSStatus {
    auto* self = static_cast<AUInstrument*>(ud);
    *bpm  = self->last_bpm_;
    *beat = self->last_beat_;
    return noErr;
};
cbi.musicalTimeLocationProc = ...;  // bar, beat, time sig
AudioUnitSetProperty(au_, kAudioUnitProperty_HostCallbacks,
                     kAudioUnitScope_Global, 0, &cbi, sizeof(cbi));
```

Values (`last_bpm_`, `last_beat_`) are updated from `VividAudioContext` each block, written before `AudioUnitRender()`.

### Note Event Translation

AU v2 is channel MIDI — no per-note expressions. The `VividNoteBuffer` → AU mapping:

| Vivid event | `MusicDeviceMIDIEvent()` call |
|-------------|------------------------------|
| `VIVID_NOTE_ON` | `0x90 \| ch, pitch, velocity, offset` |
| `VIVID_NOTE_OFF` | `0x80 \| ch, pitch, 0, offset` |
| `VIVID_NOTE_PITCH_BEND` | `0xE0 \| ch, lsb, msb, offset` (all voices share one channel) |
| `VIVID_NOTE_PRESSURE` | `0xD0 \| ch, pressure, 0, offset` (channel aftertouch) |

All voices collapse to channel 1. This is a known limitation vs. CLAP — MPE/per-voice expression is not possible in AU v2.

### State Persistence

```objc
// Save
CFPropertyListRef plist = nil;
UInt32 size = sizeof(plist);
AudioUnitGetProperty(au_, kAudioUnitProperty_ClassInfo,
                     kAudioUnitScope_Global, 0, &plist, &size);
NSData* json = [NSJSONSerialization dataWithJSONObject:(__bridge id)plist options:0 error:nil];
// base64-encode json → plugin_state param (same as CLAP)

// Load
NSData* json = [[NSData alloc] initWithBase64EncodedString:b64 options:0];
id obj = [NSJSONSerialization JSONObjectWithData:json options:0 error:nil];
CFPropertyListRef plist = (__bridge CFPropertyListRef)obj;
AudioUnitSetProperty(au_, kAudioUnitProperty_ClassInfo,
                     kAudioUnitScope_Global, 0, &plist, sizeof(plist));
```

## CMake

```cmake
if(APPLE)
  add_vivid_operator(au_instrument operators/audio/au_instrument/au_instrument.cpp CODEGEN)
  target_sources(au_instrument PRIVATE
      operators/shared/au_host/au_host_common.mm
      operators/shared/au_host/au_scanner.mm
      operators/shared/au_host/au_plugin_window.mm)
  target_link_libraries(au_instrument PRIVATE
      "-framework AudioToolbox" "-framework CoreAudio"
      "-framework AVFoundation" "-framework AppKit")
  set_source_files_properties(... PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

  add_vivid_operator(au_effect operators/audio/au_effect/au_effect.cpp CODEGEN)
  # Same framework linking
endif()
```

Both operators are `APPLE`-only throughout — no stubs or no-ops needed on other platforms.

## Phased Delivery

### Phase 1 — Headless instrument (no GUI)
- `au_host_common.mm`: `AUInstance` wrapper — instantiate, init, activate, render, deactivate, dispose
- `au_scanner.mm`: `AudioComponentFindNext()` scan for `kAudioUnitType_MusicDevice`; cache by name+vendor
- `au_instrument` operator: load by component name, receive notes, output stereo audio, transport host callbacks, state save/load
- Macro parameter mapping (same 8-slot system as `clap_instrument`)
- Test: `MidiInput` → `au_instrument` (Surge XT AU) → `audio_out`; save/reload graph with state intact

**Done when:** A user can load a `au_instrument` node, set it to Surge XT, play notes via MIDI, hear audio, and save/reload the graph with the plugin state intact.

### Phase 2 — Effect operator
- `au_scanner.mm`: extend scan to include `kAudioUnitType_Effect` and `kAudioUnitType_MusicEffect`
- `au_effect` operator: audio in → `AudioUnitRender()` → audio out
- Latency reporting via `kAudioUnitProperty_Latency`
- Passthrough when no plugin loaded

**Done when:** A reverb or EQ plugin can be inserted in an audio chain.

### Phase 3 — Plugin GUI
- `au_plugin_window.mm`: load CocoaUI bundle via `kAudioUnitProperty_CocoaUI`; instantiate `NSView` from factory class; embed in `NSWindow` (same structure as `clap_plugin_window.mm`)
- Inspector "Open Plugin GUI" button in `au_browser.h` editor
- Resize sync, show/hide, teardown on node delete / graph reload

**Done when:** Opening the editor for an AU node displays the plugin's native GUI in a separate window.

### Phase 4 — Plugin browser + MCP tools
- `au_browser.h`: searchable plugin picker (name/vendor filter, same UI kit as `clap_browser.h`)
- MCP tools: `list_au_plugins`, `set_au_plugin`, `list_au_params`, `set_au_param`

**Done when:** AU plugins are selectable from the inspector browser and fully controllable via MCP.

## Test Plugins (all free)
- [Surge XT](https://surge-synthesizer.github.io/) — full synth, excellent AU implementation, open source
- [OB-Xd](https://www.discodsp.com/obxd/) — free vintage poly synth, AU
- [Vital](https://vital.audio/) — wavetable synth (free tier), AU
- [Airwindows](https://www.airwindows.com/) — large catalog of free AU effects

## Key Risks

| Risk | Mitigation |
|------|-----------|
| No AU thread model | Only `AudioUnitRender()` and `MusicDeviceMIDIEvent()` on audio thread; all other AU calls on main thread; triple-buffer slot prevents cross-thread access |
| `CFPropertyList` serialization | `NSJSONSerialization` handles the NSDictionary→JSON conversion; base64 for param storage — same path as CLAP |
| CocoaUI bundle load | Bundle and factory class load deferred to GUI-open time (not plugin load); some plugins lack CocoaUI — disable GUI button in that case |
| No per-note expressions | Channel MIDI only — all voices share channel 1; document clearly; CLAP remains the better choice for MPE instruments |
| PACE-protected plugins | Some commercial AU plugins use iLok/PACE and will refuse to load outside authorized hosts; document as unsupported |
| AU parameter tree size | Some plugins have hundreds of params; enumerate and cache at load time, expose only in macro slots and MCP |
