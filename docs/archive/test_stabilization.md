**Status: COMPLETE** (2026-03-18)

# Test Stabilization Plan for Headless + Loader + Package Failures

## Summary

Fix the six failures in two phases:

- **Phase 1: deterministic stabilization**
  - remove segfaults, stale-plugin dependency, and brittle string assertions
  - make every affected test either pass or fail cleanly without hangs/aborts
- **Phase 2: restore strict headless coverage**
  - harden media/GPU behavior so headless runs can exercise real paths instead of depending on ambient desktop/runtime state

Chosen defaults from this planning pass:
- **Headless media goal:** full headless support, not permanent skipping
- **Test dependency model:** isolated staged fixtures per test
- **Execution style:** two-phase cleanup, not indefinite quarantine

## Key Changes

### 1. Fix `test_operator_loader` as a loader lifetime bug - DONE
Treat this as a real runtime bug, not just a flaky test.

- Audit `OperatorLoader`/`OperatorRegistry` ownership around:
  - deferred probe handles
  - custom port type registration
  - `dlclose()` timing
  - moved/unloaded loaders during test teardown
- Reproduce under ASan or guard with a focused loader-only run to identify whether the crash is:
  - double `dlclose`
  - use-after-free from descriptor/custom-type strings
  - destructor order issue in deferred probe state
- Land the runtime fix in loader/registry code, then keep `test_operator_loader` as the regression harness.
- Add one narrow regression case specifically for “all assertions pass, teardown stays clean”.

### 2. Convert flaky plugin-dependent tests to isolated staging  - DONE
Stop relying on ambient build-dir contents for tests that only need a small operator set.

Apply this to:
- `test_mixed_runtime_stability`
- `test_audio_domain_sequencer`
- any other current/future test that depends on sibling/package dylibs

Implementation approach:
- each test stages exactly the required dylibs into a private temp directory
- the registry scans only that directory
- CMake test wiring explicitly builds those plugin targets first
- tests stop depending on whatever happens to be in `build/`

Specific fixes:
- `test_mixed_runtime_stability`
  - stage `lfo`, `oscillator`, `gpu_fill_op`, and `audio_out` dependencies as needed
  - keep builtins registered, but do not assume builtins cover plugin-only operators
- `test_audio_domain_sequencer`
  - stage `clock`, `oscillator`, `gain` (core CMake-built audio operators)
  - remove ABI-mismatch risk by ensuring the test uses freshly built dylibs, not stale leftovers

### 3. Fix `test_child_op` as a build/link shape problem  - DONE
This is not a runtime flake; it is a test target shape mismatch.

- Audit why `test_child_op` includes `control/smooth/smooth.h` but still resolves `Smooth` through a missing vtable symbol.
- Decide the intended contract for child-op tests:
  - either fully header-only child operators must remain header-only, or
  - the test target must link the implementation object/source explicitly
- Make the target deterministic in CMake:
  - no dependency on unrelated dylibs at runtime
  - no hidden dynamic symbol expectation for `Smooth`
- Keep the test focused on `ChildOp`, not on incidental linker behavior.

### 4. Split `test_demo_graphs` into headless-safe smoke vs media-headless coverage - DONE
`test_demo_graphs` should not be the place where AVFoundation hangs block the whole suite.

Near-term stabilization:
- classify graphs into:
  - headless-safe smoke graphs
  - media/headless-stress graphs
- keep the default `test_demo_graphs` lane crash-free and bounded in runtime
- ensure unsupported headless paths fail fast or skip cleanly, never hang

Then restore strict coverage with a dedicated media-headless lane:
- add a separate targeted test for MovieLoaded/ColorSpace/media chains in headless mode
- require:
  - bounded startup time
  - no AVFoundation hang
  - no null GPU context abort
  - clean placeholder/error behavior if media cannot fully initialize

This preserves full headless support as a goal, while keeping the broad smoke lane reliable.

### 5. Define a proper headless media contract for MovieLoaded / AVFoundation - DONE
This is the deeper product/runtime fix behind the repeated headless problems.

Add a clear runtime contract for media operators in headless tests:
- no UI/window assumptions
- no unbounded waits on AVFoundation readiness
- no blocking startup on decode queues
- GPU/media operators must either:
  - complete a bounded placeholder/idle path, or
  - surface a recoverable runtime error state without aborting the process

Implementation targets:
- MovieLoaded startup/init path
- ColorSpace or related GPU nodes that assume a valid GPU/output context
- any queue/decoder synchronization that can wait forever in headless mode

Acceptance bar:
- headless media graphs can be executed by a dedicated automated lane without hang or abort
- if assets/services are unavailable, the graph stays alive and reports the issue cleanly

### 6. Make package/compiler error assertions resilient - DONE
`test_package_manager` should assert stable semantics, not exact wording.

- Replace brittle substring expectations like exact compiler wording with:
  - stable error code, or
  - narrower semantic fragments guaranteed by the API contract
- If the package manager does not yet expose a machine-readable failure code for missing-tool preflight, add one and assert that instead.
- Keep one human-readable remediation assertion only for the important user-facing action text.

### 7. Add test-lane structure and labels in CTest - DONE
Make failures easier to reason about and easier to run in isolation.

Create/standardize labels such as:
- `HEADLESS_SMOKE`
- `MEDIA_HEADLESS`
- `STABILITY`
- `PACKAGE`
- `LOADER`

Use them for:
- `test_demo_graphs`
- `test_mixed_runtime_stability`
- `test_audio_domain_sequencer`
- package tests
- loader tests

Also add one convenience target/regex for the problematic lane:
- headless reliability suite
- loader/package suite

## Important Interface / Contract Changes

These are the nontrivial contract-level changes implied by the fixes:

- **Loader/runtime**
  - loader teardown and deferred-probe ownership become explicitly safe across custom port metadata and destructor cleanup
- **Test harness**
  - plugin-dependent tests use explicit staged dylib directories rather than ambient build-dir scans
- **Package/compiler errors**
  - prefer stable machine-readable failure codes over exact message text in tests
- **Media/headless runtime**
  - MovieLoaded/media startup must be bounded and recoverable in headless mode; hanging is no longer an acceptable failure mode
- **CTest organization**
  - headless smoke and media-headless lanes become separate, intentional test surfaces

## Test Plan

### Immediate validation
After Phase 1, require:
- `test_operator_loader` exits cleanly with no post-assertion segfault
- `test_mixed_runtime_stability` builds its graph successfully from isolated staged operators
- `test_child_op` runs without dyld symbol abort
- `test_audio_domain_sequencer` builds an audio graph from core operators (Clock → Oscillator → Gain) with no external package dependence
- `test_package_manager` passes using stable semantic assertions
- `test_demo_graphs` no longer hangs or aborts on media/headless graphs in the default smoke lane

### Headless media validation
Add a dedicated automated lane that exercises:
- one MovieLoaded graph
- one ColorSpace/media chain
- one “media unavailable / placeholder path” scenario

Assert:
- bounded runtime
- no hang
- no abort
- no uncaught GPU/media startup failure

### Regression coverage
Add focused regressions for:
- loader teardown after deferred probe/custom type registration
- staged test-operator directory setup
- package-manager missing-tool error code/remediation behavior
- headless media startup timeout / placeholder behavior

## Assumptions And Defaults

- We are not permanently weakening coverage; temporary stabilization is allowed only to get the suite deterministic before restoring stricter headless media validation.
- Plugin-dependent tests should be isolated from ambient build state by default.
- Media operators must become headless-safe enough for automated testing, even if that means explicit placeholder/error states during startup.
- The default broad smoke lane should remain fast and reliable; heavier media-headless assertions should live in a dedicated lane once stabilized.
