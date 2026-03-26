# Operator Loading Model

## How Operators Load

Vivid operators are single-file C++ modules compiled against the `operator_api` headers.

- **Development:** Operators compile to `.dylib` files and load via `dlopen`. The hot-reload system watches for source changes, recompiles, and reloads — giving instant feedback during creative work.
- **Export:** The same operator source is compiled and statically linked into a standalone binary. No dylibs ship in production.

The `VIVID_REGISTER` macro generates the `extern "C"` entry points that the runtime uses at the dylib boundary: `vivid_abi_version`, `vivid_descriptor`, `vivid_create`, `vivid_destroy`, and domain-specific dispatch functions.

## ABI Version: A Staleness Check

`VIVID_OPERATOR_ABI_VERSION` (defined in `src/operator_api/types.h`) is a staleness detector, not a cross-version compatibility promise.

On `dlopen`, the runtime calls the dylib's `vivid_abi_version()` and compares it to the compiled-in `VIVID_OPERATOR_ABI_VERSION`. If they don't match, the load fails and a diagnostic is stored in `OperatorRegistry::abi_mismatch_by_path_`. This catches stale `.dylib` files left over from a previous build — it does not enable running old binaries against new headers.

## Packages Compile from Source

Packages are distributed as source code (git repos with `vivid-package.json`). `vivid install` clones the repo and compiles it against the current Vivid headers. When Vivid updates, recompile packages with `vivid rebuild <name>`. There are no pre-compiled binaries.

## Supporting Multiple Vivid Versions

Package authors who want to support a range of Vivid versions use compile-time `#if` guards:

```cpp
void process(const VividFrameContext* ctx) override {
    double t = ctx->time;

#if VIVID_OPERATOR_ABI_VERSION >= 12
    double dt = ctx->delta_time;  // added in a later version
#else
    double dt = 1.0 / 60.0;      // fallback for older Vivid
#endif
}
```

The compiler resolves this at build time — the dead branch doesn't exist in the compiled binary. Zero runtime cost.

A package can declare its supported range in `vivid-package.json`:

```json
{
    "name": "vivid-fancy-particles",
    "min_abi_version": 9,
    "max_abi_version": 14
}
```

The package manager checks the range before compiling. If the user's Vivid is outside the range, the install fails with a clear message.

## When to Bump the ABI Version

Bump `VIVID_OPERATOR_ABI_VERSION` when you change anything that affects the `extern "C"` boundary:

- Struct layout changes in `operator_api` headers (added/removed/reordered fields)
- Enum representation changes (e.g. `typedef enum` to `typedef uint32_t`)
- Type changes in struct fields (e.g. `bool` to `uint8_t`)
- Entry point signature changes

Add a comment on the version line documenting what changed.

## Why Vivid Doesn't Need Binary Compatibility

| | Vulkan / Windows | VST3 / AudioUnit | **Vivid** |
|---|---|---|---|
| Plugin author | GPU vendor / app developer | Plugin company | **The user + LLM** |
| Distribution unit | Pre-compiled binary | Pre-compiled binary | **Source code (GitHub)** |
| User has compiler? | No | No | **Yes (prerequisite)** |
| Plugin lifespan | Years | Years (commercial) | **Minutes to months** |
| Recompilation cost | Impossible (separate orgs) | Expensive (vendor rebuild) | **Free (vivid install)** |
| Production form | Dynamic linking | Dynamic linking | **Static linking** |
| ABI stability needed? | Absolutely | Absolutely | **No** |

Vivid's users have compilers, operators compile from source on install, and production builds use static linking. The entire rationale for binary compatibility — shipping pre-compiled plugins to users who can't recompile — doesn't apply.
