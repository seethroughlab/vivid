# File-drop wavetable import

## What it is

Drag a `.wav` file onto the editor window (or a specific zone within it) → the operator switches `wavetable_source` to "Custom" and points `wav_file` at the dropped path. User immediately sees/hears the imported table in the preview + playback.

Today the only way to load a custom wavetable is to set the `wav_file` param manually through the inspector — either by typing a path or using a file-picker dialog if one exists. File-drop is the natural modern gesture; every modern plugin supports it.

## Why deferred from v1

Drag-drop is a platform extension. The editor window needs to receive drag-enter / drag-over / drop events from the OS, figure out where the drop happened inside the window, and fire a callback to the operator's `draw_editor`. None of that exists in `VividEditorContext` today.

Like [audition](audition.md), file-drop is better done once at the platform level — every editor benefits. A sampler operator wants to drop audio files, a graph-image operator wants to drop textures, a preset-recall flow wants to drop `.json` files. Building the primitives once for WavetableOsc is wasted work; building them generically is a real win.

## Platform extension

New event type + host callback, something like:

```cpp
#define VIVID_EDITOR_EVENT_FILE_DROP 5u

typedef struct VividEditorEvent {
    // … existing fields …
    const char* drop_path;    // UTF-8 path; valid until the event is consumed
    const char* drop_mime;    // optional MIME type hint ("audio/wav", "image/png", nullptr if unknown)
} VividEditorEvent;

// Also: tell the host what file types this editor accepts, so the drag-over
// cursor can signal acceptance.
typedef struct VividEditorHostAPI {
    // … existing callbacks …
    void (*declare_accept_types)(void* opaque, const char* const* extensions, uint32_t count);
} VividEditorHostAPI;
```

Host work: integrate with GLFW drop callbacks (or native equivalents on each platform). Deliver the path as an editor event on the next frame.

## Editor-side cost (post-platform)

**~30 minutes** for WavetableOsc specifically:
- In `draw_editor`, check `ctx.events` for `VIVID_EDITOR_EVENT_FILE_DROP`. If the drop_mime starts with "audio/" or the path ends in `.wav`, call `set_string_param("wav_file", drop_path)` + `set_param("wavetable_source", SOURCE_CUSTOM)`.
- Once at init, declare accepted types: `ctx->host.declare_accept_types(ctx->host.opaque, {"wav", "aiff", "flac"}, 3)`.
- Optional: visual drop indicator (highlighted border on the preview canvas when a valid file is being dragged over).

## Why platform-first

Other operators that'd benefit:
- **Sampler / SP404-style pad loaders** — drop audio per pad.
- **GraphImage / NoiseTexture variants** — drop PNG/JPG.
- **Preset recall** — drop `.fxp` / `.json`.
- **Granular, Freeze, Delay lines** — drop audio buffers.

Without a shared platform extension, every adopter reinvents drag-drop. With it, each adopter adds ~20 lines.

## Interactions

- Doesn't interact meaningfully with any other deferred item. Self-contained.

## Scope cuts

- **Visual drop zones**: Serum has a "drop wavetable here" zone with a dashed border. Nice polish but the entire preview area being the drop target works fine for v1.5.
- **Drag-out to export**: dragging a wavetable *out* of the editor to the filesystem is a separate feature. Defer indefinitely.
- **Multi-file batch drop**: drop 8 files → auto-populate 8 member slots. Cute but rare.

## Test plan

- Pure-logic: path-to-mime dispatch (`dispatch_dropped_file(path) → param names to set`). Easy to test without any host plumbing.
- Editor: synthesize a `VIVID_EDITOR_EVENT_FILE_DROP` in the test harness → captured set_param("wav_file", path) + set_param("wavetable_source", 1).
- Platform side: integration test pending a GLFW drop fixture, or defer to manual QA.
