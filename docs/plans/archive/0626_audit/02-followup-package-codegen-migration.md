# Audit 02 Follow-up: Migrate package operators to codegen & retire VIVID_REGISTER

**Date:** 2026-06-04
**Status:** Deferred (post-audit-campaign)
**Source:** Audit 02 — `02-operator-contract-loader-registry.md` (findings 02-F5, 02-F10)

## Why

`VIVID_REGISTER` (`src/operator_api/operator.h`) is a legacy, non-codegen registration macro:
- The **core** repo uses it nowhere — every seed operator is registered by `operator_codegen`
  (`*_generated_registration.cpp`).
- The package system already ships a codegen path: `vivid_package_operator()` in
  `cmake/VividPackageSupport.cmake` runs `operator_codegen` and, when active, makes `VIVID_REGISTER`
  a no-op (`#ifdef VIVID_CODEGEN_ACTIVE`).
- But the **sibling repos build with plain `add_library(... MODULE ...)`** (e.g.
  `vivid-glitch/CMakeLists.txt:88-96`, `vivid-3d/CMakeLists.txt:91`) and rely on `VIVID_REGISTER`
  directly.

Consequences (the Audit-02 findings):
- **02-F5:** `VIVID_REGISTER` duplicates ~245 lines of ABI scaffolding already in
  `VIVID_INTERNAL_EXPORTS_WITH_DESCRIPTOR`.
- **02-F10:** `VIVID_REGISTER`'s `_vivid_get_descriptor()` never populates the v3 metadata fields
  (`display_name`/`keywords`/`summary`), so every sibling operator declaring them silently loses
  them in the chooser and MCP catalog.

Polishing/de-duplicating a doomed macro is wasted effort. The structural fix is to move the
ecosystem onto codegen and delete the macro.

## Scope (sibling repos using `VIVID_REGISTER`)

| Repo | ~operators |
|------|-----------|
| vivid-3d | 27 |
| vivid-glitch | 17 |
| vivid-wavetable | 8 |
| vivid-ml | 5 |
| vivid-physics2d | 4 |
| vivid-package-template (examples) | 4 |
| vivid-cef | 2 |
| vivid-plexus | 2 |

No sibling vendors its own `operator.h`; all include the main repo's headers via
`${VIVID_SRC_DIR}/src`.

## Plan

1. **(Optional interim, main repo)** Add the four v3-metadata getters to `VIVID_REGISTER`'s
   `_vivid_get_descriptor()` (`get_display_name` / `get_keywords_data` / `get_keywords_count` /
   `get_summary`, operator.h:456-497) so existing siblings get correct metadata on their next rebuild
   *before* migration. Mark `VIVID_REGISTER` `// DEPRECATED — use vivid_package_operator (codegen)`.
   This is throwaway-clean once the macro is deleted. (Decide whether to ship this now vs wait.)
2. **Per sibling repo:** replace the hand-rolled `add_library(... MODULE ...)` operator targets with
   `vivid_package_operator()` from `VividPackageSupport.cmake` (requires `VIVID_BUILD_DIR` wired so the
   `operator_codegen` tool is found). Rebuild; confirm operators load and now carry v3 metadata.
   - Watch for `.mm` vs `.cpp` generated-registration handling for ObjC/Metal sources
     (`VividPackageSupport.cmake:25-27`).
3. **Update `vivid-package-template`** (`single-operator` and `multi-operator`) to use the codegen path
   so new packages start modern; update template docs/AGENTS notes that mention `VIVID_REGISTER`.
4. **Once all siblings are migrated:** delete `VIVID_REGISTER` and its `_vivid_get_descriptor()` from
   `src/operator_api/operator.h`; update `src/operator_api/CLAUDE.md` and `docs/OPERATOR-LOADING.md`
   to state codegen is the only registration path.

## Verification

- Each migrated sibling builds clean and its operators load (descriptor present, `vivid_registration_mode`
  = "v2").
- An operator declaring `kDisplayName`/`kKeywords`/`kSummary` now reports them (inspect descriptor / MCP
  `operator_docs` / chooser label).
- Main-repo build stays green after the macro is deleted (no in-repo users).

## Relationship to the in-repo Audit-02 fixes

The other confirmed Audit-02 findings are self-contained and handled in the main repo independently of
this migration: 02-F3 (validation codes → constants + `OPERATOR-DESCRIPTOR-VALIDATION.md`), 02-F4
(descriptor-hash header-comment correction), 02-F7 (`test_operator_descriptor_validation.cpp`).
