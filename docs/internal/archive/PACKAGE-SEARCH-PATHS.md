# Package Search Paths & Resolution Spec (Phase 2, Step 1)

Date: 2026-03-05
Status: Accepted spec for minimal Phase 2 implementation

## Goal

Define canonical package/operator search scopes and deterministic resolution rules before implementing multi-scope loading.
This version is intentionally trimmed for faster delivery.

This spec answers:
- where packages are discovered
- how scope precedence works
- how conflicts are reported
- which config knobs control the behavior

## Scopes

Package discovery uses three scopes plus built-in fallback:

1. `local` — package roots next to the currently loaded graph
2. `workspace` — package roots in the current project/workspace
3. `user` — per-user installed/linked packages (current behavior)
4. `builtin` — core operators compiled with Vivid (final fallback)

## Canonical Directories

### Local scope

Resolved from the loaded graph file directory (`<graph_dir>`):
- `<graph_dir>/packages`
- `<graph_dir>/operators/packages`

If no graph file is loaded, local scope is empty.

### Workspace scope

Resolved from workspace root (`<workspace_root>`):
- `<workspace_root>/packages`
- `<workspace_root>/operators/packages`

`<workspace_root>` defaults to the process working directory, or the nearest ancestor containing `CMakeLists.txt` and `src/runtime` when available.

### User scope

Uses existing config-dir behavior:
- macOS: `~/Library/Application Support/Vivid/packages`
- Linux: `$XDG_CONFIG_HOME/vivid/packages` (fallback `~/.config/vivid/packages`)
- Windows: `%APPDATA%/Vivid/packages`

### System scope

Deferred for now. Not part of Phase 2 initial implementation.

## Precedence & Winner Selection

Resolution order (highest precedence first):
1. `local`
2. `workspace`
3. `user`
4. `builtin`

Rules:
- Package identity key is manifest `name`.
- First package found by precedence order wins.
- Lower-precedence packages with same `name` are ignored and reported as shadowed.

## Conflict Handling

### Across scopes (same `name`)

- Behavior: higher-precedence package wins.
- Diagnostic: warning with both paths and scopes.

### Within the same scope (duplicate `name`)

- Behavior: treat as configuration error for that scope; do not load either duplicate entry.
- Diagnostic: error with all colliding paths.

### Invalid manifest in a scanned directory

- Behavior: skip entry.
- Diagnostic: warning with path + parse reason.

## Config Knobs

### Environment variables (minimal)

- `VIVID_PACKAGE_PATHS` (optional append-only extra roots; searched after `user` and before `builtin`)

Path-list separator:
- `:` on macOS/Linux
- `;` on Windows

### Settings

Deferred for now. No settings-file knobs in initial Phase 2 rollout.

## Diagnostics Contract (for Phase 2 step 3)

`list-packages --verbose` (or equivalent API) must include:
- `name`
- `version`
- `scope` (`local|workspace|user|extra`)
- `path`
- `linked`

Shadowed/duplicate entries are emitted as resolver warnings/errors (stderr) rather than returned in the package list payload.

## Non-goals for Step 1 (trimmed)

- No resolver implementation changes yet
- No migration of existing package install paths
- No UI redesign
- No configurable scope ordering
- No per-scope settings/env override matrix
- No `system` scope in initial rollout

This step only freezes expected behavior so Phase 2 steps 2–4 can be implemented/tested deterministically.
