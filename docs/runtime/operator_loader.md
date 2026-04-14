# OperatorLoader and OperatorRegistry

## OperatorLoader

`OperatorLoader` (operator_loader.h/cpp) wraps a single operator dylib (or a built-in / shader-backed operator).
It is move-only (non-copyable).

### Loading Modes

```cpp
bool load(const char* path);                     // dlopen a .dylib/.so/.dll
void init_builtin(VividDescriptorFn, VividCreateFn, VividDestroyFn, VividProcessFrameFn);
void init_wgsl_operator(shared_ptr<WgslOperatorConfig> config); // shader-backed .wgsl operator
void unload();                                   // dlclose
bool is_loaded() const;
bool is_shader_operator() const;
const LastError& last_error() const;
```

### ABI Check (performed inside `load()`)

On load, the runtime calls `vivid_abi_version()` from the dylib and compares it to
`VIVID_OPERATOR_ABI_VERSION` as defined in `src/operator_api/types.h`. This is a staleness check, not a binary-compatibility
contract — operators always compile from source against the current headers. Mismatching ABI → load
failure, diagnostic stored in `OperatorRegistry::abi_mismatch_by_path_`.

For full loads, `OperatorLoader` also captures a structured `LastError` on failure so callers can
surface a stable machine-readable code instead of relying on stderr text.

Representative `LastError::code` values:

- `dlopen_failed`
- `missing_abi_symbol`
- `abi_mismatch`
- `missing_required_symbols`
- `null_descriptor`
- `invalid_descriptor_name`
- `hot_reload_incompatible_descriptor`
- `custom_type_registration_failed`

Authoring note for `ChildOp<T>` embeddables:

- `ChildOp<T>` consumers instantiate the concrete C++ type directly inside another plugin.
- If an operator is intended to be embeddable and still has out-of-line destructor / virtual / thumbnail definitions, those definitions must be supplied through the embeddable-support path (`*_embeddable.cpp` linked via `vivid_embeddable_op_support`).
- Otherwise loader failures may surface as ordinary `dlopen_failed` diagnostics on the consuming plugin, even though the root cause is missing embedded-use linkage rather than a bad descriptor.

The `VIVID_REGISTER(ClassName)` macro at the end of every operator .cpp generates:
```cpp
extern "C" uint32_t vivid_abi_version() { return VIVID_OPERATOR_ABI_VERSION; }
extern "C" VividDescriptorFn vivid_describe;
extern "C" VividCreateFn     vivid_create;
extern "C" VividDestroyFn    vivid_destroy;
extern "C" VividProcessFrameFn    vivid_process_frame;  // frame-rate
// + optional: vivid_process_audio, vivid_process_gpu, vivid_draw_thumbnail,
//             vivid_main_thread_update, vivid_prepare_instance_assets,
//             vivid_draw_inspector, vivid_inspector_mode,
//             vivid_file_drop_descriptor
```

### Per-Environment Dispatch

```cpp
void process_frame(void* instance, VividFrameContext* ctx) const; // frame-rate
void process_audio(void* instance, VividAudioContext* ctx) const;  // audio
void process_gpu(void* instance, VividGpuContext* ctx) const;      // GPU
```

### Optional Entry Points

```cpp
bool has_draw_thumbnail() const;
void draw_thumbnail(void* instance, const VividThumbnailContext* ctx) const;

bool has_draw_inspector() const;
uint32_t inspector_mode() const;
void draw_inspector(void* instance, VividInspectorContext* ctx) const;

bool has_file_drop_handlers() const;
const VividFileDropHandlerDescriptor* file_drop_handlers(uint32_t* count) const;

bool has_main_thread_update() const;
void main_thread_update(void* instance, double time,
                        const char** file_param_values, uint32_t file_param_count) const;

bool has_prepare_instance_assets() const;
void prepare_instance_assets(void* instance, const float* param_values,
                             const char** file_param_values,
                             uint32_t file_param_count) const;
```

`prepare_instance_assets()` is the runtime's per-instance warmup hook for heavyweight
CPU-side first-use setup. The graph compiler calls it after the node's graph param and
string/file-param values have been synchronized into the instance, both during normal
compile and hot-reload instance recreation.

## Process Context Identity

`VividFrameContext`, `VividAudioContext`, and `VividGpuContext` all carry a `node_id`
pointer populated from the compiled graph node's stable string id. This is part of the
operator ABI and is intended for operator-owned subsystems that need a stable per-instance
identity without introducing runtime-managed services. The pointer is read-only and valid
for the duration of the process callback.

Use it for:

- lookup-table generation
- waveform / wavetable bank generation
- CPU-side cache construction
- initial file-backed asset loading when the graph already provides a file param
- other one-time preparation that should not happen from `draw_thumbnail()` or another
  UI-adjacent late-use path

Do not use it for:

- direct GPU resource creation that depends on a live render/device context
- AppKit / window-system calls
- audio-device lifecycle work

### Shader-Backed Operators

When `init_wgsl_operator()` is called, the loader synthesizes a `VividOperatorDescriptor` from
the `WgslOperatorConfig`. All `const char*` pointers in the descriptor are backed by
`std::string` members on the loader (stable pointer lifetime). The `fixup_dd_pointers()` method
re-points them after a move.

---

## OperatorRegistry

`OperatorRegistry` (operator_registry.h/cpp) is the central catalog of all operator types.
It maps `type_name → OperatorLoader` and supports deferred (probe-only) loading.

`OperatorRegistry` is now treated as the **low-level** loader/catalog primitive. Runtime callers
that need on-demand operator materialization are expected to go through
`OperatorPreparationService` (`src/runtime/operators/operator_preparation_service.h`) instead of
calling lazy-loading entry points ad hoc. That keeps async UI graph transactions, blocking
runtime callers, and package/test flows on one preparation path.

### Scanning

```cpp
bool scan(const char* directory);               // full load: dlopen every dylib
bool scan_deferred(const char* directory);      // probe-only: read descriptor, don't dlopen
bool scan_shader_operators(const std::string& dir,
                           bool mark_user = false,
                           const std::string& package_name = ""); // register .wgsl files as operator types
bool load_for_graph(const Graph& graph);        // lazy-load only operators the graph uses
```

**Deferred probe**: `scan_deferred()` calls `dlopen` + `vivid_describe()` + `dlclose`,
then stores the descriptor in `deferred_`. The probe handle is kept alive in
`deferred_probe_handles_` to avoid destructor hangs on some plugins. When `find()` is called,
the loader is promoted to a full load.

### Lookup

```cpp
OperatorLoader* find(const std::string& type_name);        // may trigger lazy dlopen
OperatorLoader* find_loaded(const std::string& type_name); // never triggers load
const VividOperatorDescriptor* probe_descriptor(const std::string& type_name) const;
```

`probe_descriptor()` is the preferred read-only metadata path. It exposes deferred-probe
descriptors without forcing a dylib load, which keeps catalog/introspection surfaces cheap.

### OperatorPreparationService

`OperatorPreparationService` centralizes expensive operator materialization work:

```cpp
TaskId submit(OperatorPrepareRequest request);
OperatorPrepareResult wait(TaskId task_id);
OperatorPrepareStage task_stage(TaskId task_id) const;
```

Supported request kinds:

- `PrepareOperatorType`
- `PrepareGraphOperators`
- `ReloadPackageOperators`

The service serializes those tasks, deduplicates identical in-flight requests, and exposes a
shared stage vocabulary used by async graph/add UI flows and blocking runtime callers.

### Registration

```cpp
void register_builtin(name, desc_fn, create_fn, destroy_fn, process_fn);
void register_alias(alias_name, canonical_type_name);       // alternate name for same loader
bool register_loaded_operator(const std::string& dylib_path); // for cloned operators
```

### User-Defined Operators and Shader Operators

```cpp
void register_shader_operator(shared_ptr<WgslOperatorConfig>, bool mark_user, package_name);
void unregister_shader_operator(name);
bool is_shader_operator(name) const;
bool is_user_shader_operator(name) const;
const WgslOperatorConfig* shader_operator_config(name) const;
const std::string* shader_operator_source(name) const;
void register_user_operator(name, source_path);
bool is_user_operator(name) const;
```

Shader-backed operators are first-class operator types. A `.wgsl` file named `Blur` registers as
type `Blur`, and graphs persist that operator type directly.

### Hot-Reload

```cpp
bool reload_operator(const std::string& type_name, const std::string& new_dylib_path);
```
Replaces the `OperatorLoader` for `type_name` with a new one loaded from `new_dylib_path`.
Called by `RuntimeCore::reload_operator()` and `AudioEngine::reload_operator()`.

Hot reload is intentionally shape-conservative. The reload path preserves the previous loader when
the replacement dylib fails to load, and it rejects descriptor-incompatible edits rather than
reusing stale wire/port metadata.

Shader-backed operators follow the same philosophy. Body-only `.wgsl` edits hot-reload inside
`WgslFilterBase`, but header-derived descriptor changes (params, inputs, time-dependence, or name)
trigger a registry rescan plus full graph rebuild instead of mutating descriptors in place.

### Loader Failure Diagnostics

```cpp
std::vector<LoaderFailureDiagnostic> loader_failure_diagnostics() const;
std::vector<LoaderFailureDiagnostic> loader_failure_diagnostics_for_dir(const std::string& dir) const;
bool has_loader_failure_diagnostics() const;
```

`LoaderFailureDiagnostic` fields:

- `plugin_path`
- `plugin_name`
- `package_name`
- `code`
- `message`

These diagnostics are populated from `OperatorLoader::last_error()` when a plugin fails a full
load or reload after probing. They complement ABI-mismatch diagnostics, which only cover plugins
rejected before a full load is attempted.

### Factory Presets

```cpp
bool scan_factory_presets(const std::string& directory);
const std::vector<OperatorPreset>* factory_presets(const std::string& type_name) const;
std::vector<std::string> factory_preset_names(const std::string& type_name) const;
```

### Package Provenance

```cpp
void register_package(const std::string& package_name, const std::string& build_dir);
void unregister_package_operator(const std::string& type_name);
const std::string* package_for_type(const std::string& type_name) const;
bool is_package_operator(const std::string& type_name) const;
```

### ABI Mismatch Diagnostics

```cpp
std::vector<AbiMismatchDiagnostic> abi_mismatch_diagnostics() const;
std::vector<AbiMismatchDiagnostic> abi_mismatch_diagnostics_for_dir(const std::string& dir) const;
bool has_abi_mismatch_diagnostics() const;
```
`AbiMismatchDiagnostic` fields: `plugin_path`, `plugin_name`, `package_name`, `plugin_abi`, `runtime_abi`.

### Target ↔ Type Mapping

```cpp
const std::string* type_name_for_target(const std::string& target) const;
std::string type_to_target(const std::string& type_name) const;
```
cmake target name (e.g. `drum_kick`) ↔ descriptor name (e.g. `"Drum Kick"`).
Stored in `target_to_type_` map, populated from dylib filename conventions.

### `DeferredEntry`

Holds the full descriptor (with all owned `std::string` storage for stable `const char*` pointers)
for a probed-but-not-yet-loaded operator. Fields include: `dylib_path`, `desc`, `params`, `ports`,
all the `_names`, `_type_names`, `_stable_type_ids`, `_default_strings`, `_semantic_tags`,
conditional visibility metadata, and the owned file-drop metadata used by `file_drop_handlers()`.

### File-Drop Metadata

Operators can optionally export file-drop handlers through
`vivid_file_drop_descriptor()` / `VIVID_FILE_DROP(...)`.

`OperatorLoader` exposes that metadata directly for loaded plugins, and
`OperatorRegistry` preserves it during deferred probe so file-drop handlers are
available before the operator is fully loaded.

See `docs/runtime/file_drop_handlers.md` for the authoring contract and runtime behavior.
