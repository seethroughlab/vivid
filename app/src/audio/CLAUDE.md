# `app/src/audio/` — VST3 host + session engine

**Real-time code. Read [`../../docs/thread-safety.md`](../../docs/thread-safety.md)
before changing anything the audio callback reaches.**

- **`vst3_host.h`** — the session C API (opaque `Session`): tracks, scenes, clips,
  per-track instrument + FX chain, device params, meters/characteristics. Every
  accessor is **internally bounds-checked** (returns a safe default / no-ops on a
  bad index) — callers still validate to report `out_of_range` rather than a silent
  false success.
- **`vst3_host.cpp`** — the implementation; holds the generation-counter + `try_lock`
  edit pattern (`edit_mtx`/`edit_gen`, `fx_mtx`/`fx_gen`) and the SPSC param queue.
- **`vst3_host_common.h`** — anonymous-namespace host internals (header-only; that's
  why the session work lives in the `.cpp` and `main` only sees the C API).
- **`audio_callback.{h,cpp}`** — the miniaudio RT callback; `device->pUserData` is an
  `App*`. Renders the session (or a test tone), advances the transport, publishes
  level/3-band/transient as atomics. No allocation, no blocking locks.
- **`sampler.*`** — the audio (loop) track. **`vst3_plugin_window.*`** (.mm) — hosts a
  plugin's native Cocoa GUI (needs the `.app` bundle + foreground run loop to be
  interactive). **`vst3_vstiids.cpp`** — VST3 interface IIDs.
