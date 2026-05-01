# Tree-Sitter Source Index Implementation Plan

Status: implementation plan only. This is a follow-up to [Third-Party Library Candidates](third-party-library-candidates.md); it does not by itself approve or complete adding Tree-sitter.

## Goal

Use Tree-sitter to make C++ source-structure extraction more robust for operator documentation and symbol definition classification, without replacing Vivid's broad line-based source search.

Primary targets:

- `src/runtime/operators/operator_source_docs.cpp`
- `src/runtime/core/source_index.cpp`

Public behavior to preserve:

- `OperatorSourceDocs` JSON output and cache behavior.
- `SourceIndex::search()`, `read_file()`, `read_span()`, and broad `find_references()` text behavior.
- MCP/control-server response shapes for source search, symbol lookup, and operator docs.

Non-goals:

- Do not use Tree-sitter as a full C++ semantic analyzer.
- Do not require the Tree-sitter CLI, Node, npm, or a runtime grammar download.
- Do not parse non-C++ roots with Tree-sitter in this migration.
- Do not change operator-doc response fields unless a later user-facing doc update explicitly requires it.

## Dependency Integration

Pin both dependencies in `cmake/dependencies.cmake`:

- Tree-sitter C runtime.
- `tree-sitter-cpp` grammar.

Use generated parser sources directly and compile them into a small internal target. The runtime should call the C API and the generated `tree_sitter_cpp()` language function from compiled sources; it should not shell out to the Tree-sitter CLI or depend on Node tooling at runtime.

Keep the dependency private to runtime/source-indexing code. When the dependency is actually added, update the dependency manifest in `docs/ARCHITECTURE.md`.

## Implementation Design

Add a small internal helper, likely `SourceSyntaxIndex`, to own Tree-sitter parsing and keep parser details out of `OperatorSourceDocs` and `SourceIndex`.

Suggested records:

- type definitions: name, kind, source path, start/end line, base class names.
- registrations: `VIVID_REGISTER(<Type>)` occurrences mapped to source path and line.
- include targets: quoted include paths for recursive doc lookup.
- doc-comment ranges: adjacent block comments before type definitions.
- symbol definitions: class, struct, enum, namespace, function, alias, and macro-like definitions where Tree-sitter can represent them cleanly.

Required behavior:

- Parse only C++-like extensions: `.cpp`, `.cc`, `.cxx`, `.mm`, `.h`, `.hh`, `.hpp`.
- Keep file-size and directory-skip limits consistent with the existing indexers.
- Fall back to current text/regex behavior when parsing fails or a syntax tree is incomplete.
- Cache parsed records per root/file and invalidate through the existing `invalidate_core()`, `invalidate_package()`, and `SourceIndex::invalidate()` flows.

## Migration Stages

1. Prototype Tree-sitter in `OperatorSourceDocs` first.
   - Replace regex detection of `VIVID_REGISTER`.
   - Replace type-definition discovery and multiline base-class parsing.
   - Replace doc-block adjacency detection with comment-node/range-based lookup.
   - Preserve existing fallback behavior for wrapper classes like `ClockAu` resolving docs from shared base types.
2. Add `SourceSyntaxIndex` as the shared parsing boundary once the operator-doc prototype is stable.
3. Optionally enrich `SourceIndex::find_symbol()` definition classification for C++ files using parsed symbol definition records.
4. Keep `SourceIndex::search()`, `read_file()`, `read_span()`, and broad `find_references()` line-based because they cover non-C++ files and general text search.
5. Update `docs/runtime/control_server.md` only if the response shape or documented operator-doc fields change. A backend-only parser swap should not require runtime docs churn.

## Testing

Extend `test_operator_source_docs` with fixtures for:

- Multiline class/struct declarations.
- Templated base classes.
- Namespace-wrapped operators.
- Doc-comment adjacency and no-doc fallback.
- `VIVID_REGISTER` with whitespace or line breaks.
- Wrapper classes such as `ClockAu` and `ClockFr` resolving shared base docs.
- Malformed or incomplete C++ returning a graceful no-doc or fallback result without crashing.

Extend `test_source_index` with C++-specific fixtures for:

- Class and struct definitions versus plain references.
- Function definitions versus calls.
- Namespace and enum definitions when represented by the parser.
- Existing cross-root text search and bundled-root fallback behavior remaining unchanged.

Verification commands:

```bash
cmake --build build --target test_operator_source_docs test_source_index
ctest --test-dir build --output-on-failure -R "operator_source_docs|source_index"
```

## Acceptance Criteria

- Tree-sitter is pinned and compiled into the build without runtime CLI or Node requirements.
- `OperatorSourceDocs` output remains compatible with existing callers while handling multiline and namespace-wrapped C++ more robustly.
- `SourceIndex` broad search/read behavior remains line-based and multi-language.
- C++ `find_symbol()` definition classification improves only where parsed data is available, with fallback for parse failures.
- Tests cover operator-doc extraction, source-index classification, malformed C++ fallback, and unchanged public response shapes.

## Assessment Notes (2026-04-08)

**Recommendation: defer until a concrete failure motivates the work.**

### Current State

Both target files are modest in size (~600 lines each) and functioning without known bugs or TODOs. The regex patterns are theoretically brittle — particularly the function-definition detection in `source_index.cpp` (doesn't handle templates, trailing return types, `noexcept`, or multi-line signatures) — but no actual failures have been reported.

### Benefits if implemented

- Robust handling of multiline declarations, templated base classes, and complex C++ syntax in operator doc extraction and symbol classification.
- Cleaner separation between structural parsing and text search.
- Foundation for richer MCP symbol navigation in the future.

### Reasons to defer

- **No concrete pain point.** The regex approach works for the operator patterns actually in use.
- **Dependency cost.** Tree-sitter runtime + C++ grammar adds compile-time and binary size for a narrow internal use case (opdev tooling, not user-facing).
- **Dual-maintenance burden.** The plan requires keeping regex fallbacks for parse failures, meaning both approaches must be maintained.
- **Scope of work.** Five migration stages for infrastructure that isn't broken.
- **Scope creep risk.** Having a C++ AST available invites leaning on it more broadly, increasing maintenance surface.

### When to revisit

- An operator with a complex declaration (heavy templates, multi-line inheritance, namespace wrapping) produces wrong or missing docs.
- Symbol classification gives wrong results that affect the MCP/opdev workflow.
- A broader initiative (e.g., richer code navigation, refactoring tools) would benefit from AST-level understanding.
