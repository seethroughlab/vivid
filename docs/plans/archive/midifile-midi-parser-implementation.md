# Midifile MIDI Parser Implementation Plan

Status: implementation plan only. This is a follow-up to [Third-Party Library Candidates](third-party-library-candidates.md); it does not by itself complete or approve the dependency migration.

## Goal

Replace Vivid's compact in-house Standard MIDI File parser with a small adapter around [Midifile](https://github.com/craigsapp/midifile), while preserving the current public API:

```cpp
vivid::midi_file::Sequence vivid::midi_file::parse_file(const std::string& path);
```

Primary target:

- `src/common/midi_file.cpp`

Public types to preserve:

- `Event{time_seconds, status, data1, data2}`
- `Sequence{events, duration_seconds, error}`
- `Sequence::ok()`

Non-goals:

- Do not expose Midifile types in `src/common/midi_file.h`.
- Do not add MIDI editing, writing, lyrics, marker, or metadata APIs in this migration.
- Do not change operator-facing playback behavior beyond accepting files Midifile can parse more robustly.

## Dependency Integration

Add Midifile as a pinned dependency in `cmake/dependencies.cmake`. Prefer FetchContent if its CMake target integrates cleanly; otherwise vendor a pinned source directory under `deps/midifile` and create a small static library target from the needed source files.

Midifile's documentation describes `MidiFile` as the primary class for reading and writing Standard MIDI Files. It also notes that the optional `Options` helper is not needed for using the `MidiFile` class, so exclude `Options`, command-line tools, examples, and programs from the Vivid build unless a future tool explicitly needs them.

Expected integration shape:

- Link the Vivid target and MIDI parser tests against the new Midifile target.
- Keep Midifile includes private to `midi_file.cpp`.
- Pin a tag or commit rather than tracking Midifile `master`.
- Do not introduce Homebrew, vcpkg, or another package-manager dependency for the core build.

## Adapter Behavior

Implement `parse_file()` as a compatibility adapter:

- Construct `smf::MidiFile` and read from `path`.
- On read failure, return a `Sequence` with a stable error string as close as practical to the current parser's wording.
- Call Midifile time analysis before collecting events so `time_seconds` reflects tempo changes.
- Iterate tracks and events, collecting only channel events currently represented by Vivid:
  - note off
  - note on
  - poly pressure
  - control change
  - program change
  - channel pressure
  - pitch bend
- Preserve the current note-on velocity-zero normalization by converting `0x90` events with velocity 0 into note-off status for the same channel.
- Keep meta and sysex events internal or ignored. Tempo meta events should affect time analysis but should not appear in `Sequence::events`.
- Preserve current format behavior where practical: format 0 and 1 must work; SMPTE timing should keep returning an explicit unsupported error unless the migration intentionally expands that behavior with tests.
- Preserve deterministic event ordering. If Midifile returns per-track ordering, merge events into one vector and sort by `time_seconds`, then status/data bytes to keep output stable.
- Set `duration_seconds` to the final analyzed event time, matching the current "at least the last emitted event time" behavior.

Do not let callers depend on Midifile track indexes, meta-event objects, or raw MidiFile lifetime.

## Migration Steps

1. Add the pinned Midifile target and verify Vivid still configures.
2. Replace the manual byte reader in `src/common/midi_file.cpp` with the Midifile adapter while leaving `midi_file.h` unchanged.
3. Keep the old parser behavior covered by tests before broadening behavior. If Midifile accepts a file that the old parser rejected, prefer the old error behavior unless the new acceptance is intentional and documented in the test name.
4. Remove now-unused parsing helpers from `midi_file.cpp`.
5. Update dependency documentation if the dependency is actually added, especially the dependency manifest in `docs/ARCHITECTURE.md`.

## Testing

Update or add MIDI parser tests for the backend-independent contract:

- Format 0 file parses into the same `Sequence` shape as before.
- Format 1 file merges events across tracks deterministically.
- Tempo changes affect `time_seconds`.
- Running status parses correctly.
- Note-on velocity 0 is normalized to note off.
- Malformed header and malformed track data return errors.
- SMPTE timing preserves the current unsupported behavior unless explicitly expanded.
- Checked-in fixtures produce parity with the existing parser's event count, status/data bytes, and approximate event times.

Verification commands:

```bash
cmake --build build --target test_midi_file
ctest --test-dir build --output-on-failure -R "midi"
```

If the exact test target name differs, use the existing MIDI parser test target or add one before migrating the parser.

## Acceptance Criteria

- `src/common/midi_file.h` remains source-compatible for callers.
- `src/common/midi_file.cpp` no longer owns a hand-written Standard MIDI File parser.
- Midifile is pinned and linked only where needed.
- Existing simple MIDI playback behavior is unchanged.
- Tests cover format 0, format 1, tempo, running status, velocity-zero note-on normalization, malformed input, and SMPTE behavior.
