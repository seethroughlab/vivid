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
  **Plugins are graph NODES** (A2): `session_audio_graph_add_plugin` puts a VST3/CLAP
  instrument or effect anywhere in a track's graph. Its handle lives in a per-track
  `PluginSlot` addressed by a stable index — *not* by its position in the chain, which is
  what the old kind+order rebind assumed. A node whose plugin is still loading (CLAP is
  async) is already RT-safe: `run_track_graph` gates on the handle being non-null, so it
  passes audio through / stays silent until it binds. Handles are **retired, never freed**
  on removal (the audio thread may hold the pointer for one more block).
- **`plugin_catalog.{h,cpp}` / `plugin_class.h` / `plugin_probe.{h,cpp}` / `plugin_cache.*` /
  `plugin_scan.*`** — the ONE catalog of installable things: every VST3 + CLAP on the machine,
  each classified instrument/effect by a **background probe that runs OUT OF PROCESS**
  (`--probe-plugin`). That is not paranoia: probing in-process crashed the app on two installed
  plugins, one per launch. Verdicts are cached to `plugin_cache.json` (keyed on the bundle's
  *executable* mtime/size — a `.vst3` is a directory, so an installer can replace the binary
  inside it without touching the directory's mtime). A plugin that crashes or hangs the probe is
  recorded and never opened again. Classification reads the plugin's **factory descriptor only** —
  no instantiation, which is why Surge XT classifies in milliseconds instead of ~90 seconds.
  **There is no hard-coded plugin list any more**; names resolve against the whole catalog
  (`session_add_effect_by_name`), which is only used to load OLD projects (they saved effects by
  name, not path).
- **`vst3_host_common.h`** — anonymous-namespace host internals (header-only; that's
  why the session work lives in the `.cpp` and `main` only sees the C API).
- **`audio_callback.{h,cpp}`** — the miniaudio RT callback; `device->pUserData` is an
  `App*`. Renders the session (or a test tone), advances the transport, publishes
  level/3-band/transient as atomics. No allocation, no blocking locks.
- **`sampler.*`** — the audio (loop) track. **`vst3_plugin_window.*`** (.mm) — hosts a
  plugin's native Cocoa GUI (needs the `.app` bundle + foreground run loop to be
  interactive). **`vst3_vstiids.cpp`** — VST3 interface IIDs.
