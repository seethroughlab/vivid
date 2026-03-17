# Operator-Owned File Drop Handlers

Vivid supports operator-owned drag-and-drop file handlers. An operator can declare
which file extensions it knows how to open, and the runtime can create that
operator directly from a dropped file.

This is intentionally operator-owned:

- the operator is the source of truth for what it can open
- sibling packages participate automatically through normal operator discovery
- there is no package-manifest registration layer to keep in sync

## Runtime Behavior

When a file is dropped onto the graph:

1. `.json` stays reserved for graph loading.
2. Other paths are matched by file extension, case-insensitively.
3. If one operator matches, Vivid creates it immediately.
4. If multiple operators match, Vivid opens a chooser at the drop location.
5. The dropped absolute path is written into the operator's declared file param.

Node creation reuses the normal runtime/UI path:

- `try_add_node(...)`
- `set_node_layout(...)`
- `set_string_param(...)`

## Authoring Contract

Operators opt in by exporting `vivid_file_drop_descriptor()` through the
`VIVID_FILE_DROP(...)` macro in `operator.h`.

Example:

```cpp
static const char* kImageExts[] = {".png", ".jpg", ".jpeg"};
static const VividFileDropHandlerDescriptor kFileDrops[] = {{
    "Load Image",
    3,
    kImageExts,
    "file",
    100,
    "Create a TextureLoader node from a dropped image."
}};

VIVID_REGISTER(TextureLoader)
VIVID_FILE_DROP(kFileDrops)
```

## Descriptor Fields

Each `VividFileDropHandlerDescriptor` contains:

- `label`
  - user-facing action label shown in the chooser
- `extension_count`
- `extensions`
  - lowercase extensions including the leading `.`
- `file_param`
  - the param name that receives the dropped absolute path
- `priority`
  - ordering hint only; does not suppress the chooser when multiple handlers match
- `description`
  - optional short help text

## Validation Rules

The runtime ignores malformed handlers.

Requirements:

- `extensions` must be present and non-empty
- `file_param` must exist on the operator descriptor
- `file_param` must be `VIVID_PARAM_FILE` or `VIVID_PARAM_TEXT`

Extension matching is normalized to lowercase at runtime.

## Chooser Behavior

If multiple operators register the same extension:

- all valid handlers stay available
- entries are ordered by descending `priority`, then label/type name
- the chooser shows:
  - handler label
  - operator type
  - package provenance when available

This keeps ambiguous families workable without forcing a global policy too early.

## Reserved Behavior In v1

- `.json` remains reserved for graph loading
- matching is extension-first, not MIME/sniffing-first
- the dropped absolute path is written directly into the declared file param

More semantic families such as MIDI, GIF, TXT, HTML, ONNX, LUTs, and fonts can
build on the same operator-owned model later.

## Single-File Eligibility Rule

Register a file type only on operators that can do something complete and
truthful from a single dropped file of that type.

Having a `file` param is not enough. The chooser should only show operators that
are honest one-file destinations.

Examples:

- `.wav`
  - `Sampler`
  - `Slicer`
- `.mid`, `.midi`
  - `MidiFilePlayer`

Non-example:

- `SP404` is intentionally excluded from `.wav` registration because it is
  designed around a broader sample-bank / pad workflow, not a single dropped
  sample.

## Seed Examples

Current seed examples:

- core:
  - `TextureLoader` handles `.png`, `.jpg`, `.jpeg`
  - `MidiFilePlayer` handles `.mid`, `.midi`
- sibling package:
  - `MeshImport` in `vivid-3d` handles `.glb`, `.gltf`, `.obj`
  - `Sampler` / `Slicer` in `vivid-sampler` handle `.wav`
