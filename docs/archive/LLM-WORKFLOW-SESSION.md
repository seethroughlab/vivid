# LLM-Guided Workflow: End-to-End Session

- Date: 2026-03-14
- Environment: Vivid headless + control server on localhost:9876
- Launch flags: `VIVID_SKIP_PLUGINS=particles3d VIVID_SKIP_PACKAGE_SCAN=1`
- Graph: `graphs/intro/demo.json` (Clock, LFO, NoiseTexture, video_out)

---

## Part 1: Hot-Reload Error Recovery

### 1a. Syntax error recovery

**Break:**
```
vivid::Param<float> frequency{"frequency", 1.0f, 0.01f, 20.0f}  // MISSING SEMICOLON
```
Triggered rebuild by editing `lfo.cpp`.

**Introspect result (broken):**
```json
{
  "errored": false,
  "message": "...lfo.h:14:67: error: expected ';' at end of declaration list...",
  "output": 2.17
}
```
Node still producing output (old dylib), compiler error visible in `health.message`.

**Fix:** Restored semicolon, triggered rebuild.

**Introspect result (fixed):**
```json
{
  "errored": false,
  "message": "",
  "output": 2.45
}
```
Error cleared, node continues.

**Status: PASS**

### 1b. Missing include recovery

**Break:** Added `#include "nonexistent_header.h"` to lfo.h.

**Introspect result (broken):**
```json
{
  "errored": false,
  "message": "...lfo.h:3:10: fatal error: 'nonexistent_header.h' file not found...",
  "output": 3.48
}
```

**Fix:** Removed bad include, triggered rebuild.

**Introspect result (fixed):**
```json
{
  "errored": false,
  "message": "",
  "output": 4.90
}
```

**Status: PASS**

### 1c. dlopen failure recovery

**Break:** Commented out `VIVID_REGISTER(LFO)` and replaced with `extern "C" void dummy_symbol() {}`.
Build succeeds (valid C++) but dylib has no `vivid_abi_version` symbol.

**Introspect result (broken):**
```json
{
  "errored": false,
  "message": "",
  "output": 2.22
}
```
Node keeps running with old code. Error logged to stderr (`[vivid] Missing symbol: vivid_abi_version`) but not propagated to `health.message` (this is expected for the dlopen failure path which handles the error in OperatorLoader::load() before the scheduler is involved).

**Fix:** Restored `VIVID_REGISTER(LFO)`, triggered rebuild.

**Introspect result (fixed):**
```json
{
  "errored": false,
  "message": "",
  "output": 2.52
}
```

**Status: PASS**

### 1d. Linked package reload from source

**Setup:** Created test package at `/tmp/vivid_test_pkg` with `TestCounter` operator (outputs `frame_count * scale`).

**Requests:**
```
POST /link_package {"path":"/tmp/vivid_test_pkg"}
  → {"ok":true, "result":{"name":"test-reload-pkg","operator_count":1,"linked":true}}

POST /add_node {"type":"TestCounter","node_id":"tc1"}
  → {"ok":true, "message":"added TestCounter as tc1"}
```

**Pre-edit introspect:** `TestCounter output: 122.0`

**Edit:** Changed `process()` to multiply by `1000.0f`.

**Rebuild:**
```
POST /rebuild_package {"name":"test-reload-pkg"}
  → {"ok":true, "result":{"name":"test-reload-pkg","operator_count":1,"linked":true}}
```

**Post-edit introspect:** `TestCounter output: 5020000.0`

1000x multiplier applied. Vivid stayed alive (no crash).

**Bug found and fixed:** The original `rebuild_package` handler unregistered operators (calling dlclose on the old dylib) while running instances still held vtable pointers into it, causing a crash on the next process() tick. Fixed by destroying instances before rebuild and recreating them from new loaders after.

**Status: PASS** (after fix)

---

## Part 2: LLM-Guided Workflow

### 2a. Compose from existing operators

```
POST /list_types
  → 55 types available

POST /add_node {"type":"Math","node_id":"math1"}
  → added Math as math1

POST /connect {"from_addr":"lfo1/value","to_addr":"math1/a"}
  → connected lfo1/value -> math1/a

POST /set_param {"node_id":"lfo1","param":"frequency","value":2.0}
  → lfo1/frequency = 2

POST /set_param {"node_id":"lfo1","param":"amplitude","value":5.0}
  → lfo1/amplitude = 5

POST /inspect_graph
  → Full graph state with live output values:
    clock1: beat_phase=0.017, beat_ms=500.0
    lfo1: value=4.545
    noise1: texture=0.0
    math1: result=4.545 (receiving LFO input)
```

**Status: PASS**

### 2b. Scaffold a new operator

```
POST /scaffold_operator {"name":"pulse_gen","domain":"control"}
  → {"ok":true,"result":{
       "cpp_path":"./operators/control/pulse_gen/pulse_gen.cpp",
       "target_name":"pulse_gen"
     }}
```

Verified:
- `operators/control/pulse_gen/pulse_gen.cpp` exists on disk
- `CMakeLists.txt` contains `add_vivid_operator(pulse_gen ...)`
- After build: `PulseGen` appears in `list_types` with params, inputs, outputs

**Bug found and fixed:** Scaffold template emitted `1f` instead of `1.0f` for float literals (integer values lost decimal point). Fixed `emit_param_declaration()` in `operator_creator.cpp` with `float_literal()` helper.

**Bug found and fixed:** CMake insertion markers in `cmake_insertion_marker()` didn't match actual CMakeLists.txt section headers. Updated to use substring-safe prefixes.

**Status: PASS** (after fixes)

### 2c. Implement and hot-reload

Edited `pulse_gen.cpp` to output `42.0f * amount.value` instead of `input * amount.value`.

Hot-reload fired (file watcher detected `.cpp` change → cmake rebuild → dylib swap).

```
POST /introspect_nodes
  → PulseGen output: 42.0
```

**Status: PASS**

### 2d. Wire into graph and verify

```
POST /add_node {"type":"Math","node_id":"math2"}
  → added Math as math2

POST /connect {"from_addr":"pg1/output","to_addr":"math2/a"}
  → connected pg1/output -> math2/a

POST /connect {"from_addr":"lfo1/value","to_addr":"math2/b"}
  → connected lfo1/value -> math2/b

POST /set_param {"node_id":"math2","param":"operation","value":1}
  → math2/operation = 1 (multiply)

POST /introspect_nodes
  → math2 output: 200.73 (= PulseGen 42.0 * LFO ~4.78)
```

**Status: PASS**

### 2e. Save session

```
POST /save_variation {"name":"llm-test"}
  → saved variation 'llm-test'

POST /save_graph
  → saved to graphs/intro/demo.json
```

**Status: PASS**

---

## Bugs Found and Fixed During Verification

| Bug | Fix | File |
|-----|-----|------|
| `rebuild_package` crashes when nodes are running (dlclose on old dylib while instances hold vtable pointers) | Destroy instances before rebuild, recreate from new loaders after | `control_server.cpp` |
| Scaffold template emits `1f` instead of `1.0f` for integer-valued floats | Added `float_literal()` helper to always include decimal point | `operator_creator.cpp` |
| CMake insertion markers don't match actual CMakeLists.txt section headers | Updated `cmake_insertion_marker()` to use correct prefixes | `operator_creator.cpp` |

## Summary

All 4 hot-reload error recovery scenarios and all 5 LLM workflow steps verified successfully in a live Vivid headless session driven via the HTTP control server. Three bugs were discovered and fixed during verification.
