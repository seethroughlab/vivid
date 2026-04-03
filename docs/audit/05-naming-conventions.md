# Phase 5: Naming & Convention Consistency

**Date:** 2026-04-03
**Status:** Complete

## Summary Table

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| N-01 | Info | File Naming | All 187 source files use snake_case — perfect consistency | All |
| N-02 | Info | Class/Struct | All classes and structs use PascalCase | All |
| N-03 | Info | Functions | All functions and methods use snake_case | All |
| N-04 | Info | Variables | Member variables use trailing underscore, locals snake_case | All |
| N-05 | Info | Constants | C++ constants use kCamelCase consistently | All |
| N-06 | Low | Enum Values | 95% PascalCase, but 2 enums use kPrefix style | `inspector_layout.h`, `dialog_types.h` |
| N-07 | Info | Namespaces | Clean hierarchy (`vivid`, `vivid::ui`, etc.), no global pollution | All |
| N-08 | Info | Include Guards | 98% use `#pragma once` (2 legacy `#ifndef` exceptions) | All |
| N-09 | Info | Macros | All use VIVID_UPPER_SNAKE_CASE with proper namespacing | `operator_api/types.h` |

## Severity Definitions

Same scale as Phase 1.

---

## Assessment: Excellent

The codebase demonstrates professional-grade naming consistency across all 9 categories examined. Only one trivial inconsistency found.

---

## Findings

### N-01 through N-05: Perfect consistency across core conventions [Info]

| Convention | Style | Violations |
|-----------|-------|------------|
| File names | `snake_case.h` / `snake_case.cpp` | 0 |
| Classes/structs | `PascalCase` | 0 |
| Functions/methods | `snake_case()` | 0 |
| Member variables | `trailing_underscore_` | 0 |
| Local variables | `snake_case` | 0 |
| C++ constants | `kCamelCase` | 0 |
| Macros | `VIVID_UPPER_SNAKE_CASE` | 0 |
| Namespaces | `vivid`, `vivid::ui` | 0 |
| Include guards | `#pragma once` | 2 legacy exceptions |

### N-06: Enum value naming has minor inconsistency [Low]

**What:** 95% of enum values use PascalCase (`Pointwise`, `Direct`, `Snapshot`). Two enums use kPrefix style:

```cpp
// inspector_layout.h
enum class RowMode : uint8_t { kFull, kTwoUp, kMultiUp, kCompound };

// dialog_types.h
enum class SaveConfirmAction { kNewGraph, kNewProject };
```

**Dominant pattern** (used everywhere else):
```cpp
enum class LaneBehavior : uint8_t { Pointwise, Structural, Reduction, Kernel };
enum class EdgeTransport : uint8_t { Direct, Snapshot };
enum class BridgeKind : uint8_t { None, Hold, Snapshot, LastSample, Rms, Peak, Waveform };
```

**Recommendation:** Convert the 2 outlier enums to PascalCase to match the dominant pattern. Trivial effort.

---

## Well-Organized Areas

The codebase follows a clear, consistent style throughout:
- **Operator API (`types.h`):** C-compatible macros use `VIVID_` prefix with `UPPER_SNAKE_CASE` for all ABI constants
- **Namespace hierarchy:** `vivid` (core), `vivid::ui` (GUI), `vivid::gpu` (GPU utils), with sub-namespaces for domain helpers (`vivid::draw_plot`, `vivid::draw_ui`)
- **Header/source pairs:** Every `.h` has a matching `.cpp` (or is intentionally header-only)

## Action Items

None required. One trivial cosmetic fix available (N-06) if desired.
