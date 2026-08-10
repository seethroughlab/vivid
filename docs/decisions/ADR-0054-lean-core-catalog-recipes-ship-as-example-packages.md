# ADR-0054: Built-in Operators Teach Authoring — Keep the Core a Lean Substrate; Recipes and Single-Look Ops Ship as Editable Examples

Status: accepted (implemented — Stage 1 #290; move-out #291/#292/#296/#297/#298; shader curation #299)

Date: 2026-08-08

## Context

**The premise.** Vivid's value is not its built-in operator catalog — it is the **core structures you
build on and the operators you author yourself.** The built-ins exist mostly to *teach* that: to show
what composing primitives (or writing a small operator) looks like, so a user copies, modifies, and
authors rather than only wiring pre-made nodes. Because Vivid is **LLM-empowered**, "make your own
operator" is a realistic default — not an expert-only path — so the product is the **plumbing that
makes authoring frictionless**, and the catalog's job is to *invite* authoring, not substitute for it.

That premise puts a bloated built-in catalog on the wrong side of the lesson. Today Vivid ships **~54
bundled visual operators** plus 11 native audio ops — all package dylibs built by
`add_vivid_operator(...)` (`app/operators/CMakeLists.txt`), copied into `PlugIns/`, and `dlopen`'d at
startup. Two packages dominate: `core-visuals` (24 ops — the mesh pipeline, `image`/`switch`/`output`/
`customshader`, and workflow nodes like `emitter`/`instancer`/`solids`/`shape_grid`/`lines`) and
`vivid-3d` (19 ops — the scene-graph + reactivity spine + lane primitives, plus content variety like
`particles3d`/`deformer`/`instances_from_signal`). A catalog that large reads as "here is a big set of
finished effects to pick from" — which teaches the opposite of the premise.

Alongside them, two packages already model the *right* posture — `example-visuals` (`Gradient`) and
`example-audio` (`Drive`, `SineSynth`, `PulseGen`) — each a directory with a `vivid-package.json`
compiled at install/load by the package compiler (`app/src/packages/package_compiler.cpp`), not
pre-built into the bundle. The same machinery supports **project-local** packages:
`install_package(..., out_dir)` accepts a project folder, and `discover_packages(clones, ...)` scans
them at launch — so an operator's *source* can ship inside the project that uses it, visible and
editable. The plumbing for "not core — a compilable, editable example that lives with its project"
exists and is proven. What is missing is a **policy** for which operators are core *substrate* versus
that example surface.

Two things make this the right decision (and one non-goal):

1. **ADR-0046 already split the catalog in spirit.** First-class operators are *composable
   primitives*; `Instancer`, `Emitter`, `Solids`, `InstancesFromSignal` are *recipes* — workflow-shaped
   nodes that teach a single path. It demoted them in ranking but left them bundled. This ADR asks the
   real question: should a recipe be *bundled core* at all, or is its right home an **editable example**
   a user opens, reads, and forks into their own operator?
2. **ADR-0039** commits to a package/registry future. A lean core with a healthy example-package
   surface is the on-ramp to a world where users and the LLM routinely author and share operators —
   dogfooded first on our own content ops.

**Non-goal — this is not about size or cost.** It is tempting to justify a lean core by the per-op
Definition-of-Done upkeep (ADR-0042) or bundle weight; those are real but they are *side effects*, not
the reason. The reason is **posture**: the core should read as *primitives + editable examples that
invite you to author*, not a finished catalog that invites you to only consume. If leanness ever
conflicts with keeping a genuinely reusable primitive discoverable, keep the primitive.

**A hard constraint shapes the answer.** The install-time compiler links **only** `wgpu_native` and
the `operator_api/` headers (`package_compiler.cpp:157`) — no project link step for extra libraries.
So an operator that needs anything beyond `operator_api` **cannot** become a compile-at-load package:

- `Model` / `MeshLoad` link `nlohmann_json` (glTF parsing).
- `Text` / `VectorText` / `NoteType` link `freetype` + a bundled font path.
- `Video` / `Webcam` link AVFoundation/HAP/CoreMedia frameworks — the CMake comment already calls
  Webcam "the one native operator [the] install-time clang++ package route can't [build]".

These are heavy, but they are *pinned to core by their dependencies*, not by catalog posture. The
operators that are genuinely free to move are the **pure `operator_api` + `webgpu` ops** — which,
not coincidentally, are exactly where the recipe and single-look content nodes live.

## Decision

Define **core** as the lean **substrate every project builds on** — the composable primitives, the
render/reactivity/audio spine, and the ops pinned there by dependencies — and move eligible **recipes**
and **single-look content ops** out into **editable examples**: an installable package, or the
operator's source carried *inside the project that uses it*. What remains bundled should read as
*primitives to compose and examples to fork into your own operator*, not a finished catalog to consume.
Adopt a two-part test.

### What stays in core (the spine)

An operator is core if it passes **any** of:

- **Spine** — nothing renders, reacts, or sounds without it: `Render3D`, `SceneMerge`, `Output`;
  the reactivity sources `ReactiveMaster`, `ReactiveTrack`, `AudioSpectrum` (ADR-0053); the native
  audio spine `Sampler`, `Bitcrush`, `SVFilter`, `LFO`, `ADSR`, `Arp`, `TestTone`.
- **Composable primitive** (ADR-0046 role = source / transform / adapter / renderer): `Image`,
  `CustomShader` (the shaders-as-content spine, ADR-0016), `Shape3D`, `Light3D`, `Feedback`,
  `Bloom`, `Switch`, `Switch3D`, `Clock`, and the lane primitives `LaneRamp`, `LanePalette`,
  `InstancesFromLanes`, `Instancer3D`, plus the geometry pipeline `Mesh`/`MeshRender`/`MeshDisplace`.
- **Dependency-pinned** — it needs libraries the install-time compiler can't link, so a package is
  not a real option today: `Model`, `MeshLoad`, `Text`, `VectorText`, `NoteType`, `Video`, `Webcam`.
  (These *stay*, but they carry the most bundle weight and are the first candidates for a future
  "optional bundled pack" if we ever want a truly minimal install — see Follow-ups.) **Note the
  pinning is not uniform:** `Model`/`MeshLoad` only depend on `nlohmann_json`, which is *header-only*
  and already includable via the compiler's `-I <package_dir>` — so they become movable the moment
  packages can vendor headers (Stage 1 below), *before* any real linker work. `Video`/`Webcam` need
  system frameworks (Stage 2); `Text`/`VectorText`/`NoteType` need a *built* FreeType lib + a font
  asset (Stage 3) and are the last to move.

### What moves to example packages

An operator is a **move-out candidate** if it is a **pure `operator_api` + `webgpu` op** (no extra
link deps, so the compiler can build it) **and** either:

- it is an **ADR-0046 recipe** (bundles acquisition + layout + material + render into one "drop for
  this look" node), or
- it is a **single-look content op** — one attractive but non-fundamental variation that a user could
  rebuild from primitives, and that mainly earns its place as an *example* of recombination.

Concrete candidate list (all verified pure in `app/operators/CMakeLists.txt`):

| Operator | Package today | Why it moves | Target |
|---|---|---|---|
| `Emitter` | core-visuals | ADR-0046 recipe (signal → lifecycle+layout+color+render) | `examples/recipes-visual` |
| `Instancer` | core-visuals | ADR-0046 recipe | `examples/recipes-visual` |
| `Solids` | core-visuals | ADR-0046 recipe | `examples/recipes-visual` |
| `InstancesFromSignal` | vivid-3d | ADR-0046 recipe (the "split over time" adapter) | `examples/recipes-visual` |
| `ShapeGrid` | core-visuals | single-look content (grid of shapes) | `examples/content-visual` |
| `Lines` | core-visuals | single-look content | `examples/content-visual` |
| `InstanceGrid` | vivid-3d | single-look layout variant of the lane primitives | `examples/content-visual` |
| `InstanceNoise` | vivid-3d | single-look layout variant | `examples/content-visual` |
| `Deformer` | vivid-3d | single-look transform (rebuildable from `MeshDisplace`/SDF) | `examples/content-visual` |
| `Particles3D` | vivid-3d | single-look content sim | `examples/content-visual` |
| `TimeMachine` | core-visuals | legibility-polish content op (ADR-0050 pack) | `examples/content-visual` |
| `CosinePalette` | core-visuals | legibility-polish content op | `examples/content-visual` |

> **Move-out progress.** Slice 1 (implemented): **`TimeMachine` + `CosinePalette` moved to the
> installable `app/operators/packages/content-visual` package** and dropped from the bundled build —
> the first ops *truly* removed from the default install (Model A). Both are pure `operator_api` +
> WebGPU, so they compile on install with no vendoring. Chosen first because **zero shipped example
> project references them** (no migration needed). `InstanceNoise` was in the intended first batch but
> was **deferred**: it depends on vivid-3d's package-local `operator_api/gpu_3d.h` + `thumbnail_3d.h`,
> so moving it standalone needs those 3D headers vendored (Stage 1) — best done as a vivid-3d-content
> slice, not mixed into a core-visuals content package.
>
> Slice 2 (implemented): **`InstanceNoise` moved to the installable `app/operators/packages/content-3d`
> package** — the first move-out that exercises Stage 1 vendored includes. It needs vivid-3d's
> package-local 3D header shim (`operator_api/gpu_3d.h`, `thumbnail_3d.h`, `instance_algorithms.h`,
> `lane_thumb.h`, `port_type_registry.h`) + `linmath.h`, none of which live in the shared
> `operator_api/`. The package **vendors copies** of them and declares `dependencies.vendor`, so the
> package compiler adds `-I vendor` and it builds on install with no external libraries. Chosen because
> `InstanceNoise` is **unused by any shipped project** — pure dead weight in core. Its vivid-3d siblings
> `InstanceGrid` / `Deformer` / `Particles3D` **stay for now**: they back shipped demos
> (`lattice` / `crystal` / `blob` / `storm`), so moving them Model-A-style needs a *creative* demo
> migration first — not done unilaterally.
>
> Slice 3 (implemented): **`InstanceGrid` + `Deformer` moved to `content-3d`.** Key correction to the
> "demo-gated" framing above: auditing the **tracked** demos (`git`-committed: drift, generative-fields,
> geometry, grid, signal, surge-lead) shows **none** of them use `InstanceGrid`/`Deformer`/`Particles3D`/
> `InstancesFromSignal`/`Emitter`/`Instancer`/`Solids` — the demos that do (`crystal`/`lattice`/`blob`/
> `storm`/…) are **untracked** in the repo. So moving these ops breaks nothing shipped and needs no demo
> change. The carry-the-package migration *was* validated on the local crystal/lattice demos (crystal
> 0.177→0.172, lattice 0.358→0.351 mean brightness — parity), and remains the approach for the two ops
> that a *tracked* demo does use — **`ShapeGrid`** (geometry, surge-lead) and **`Lines`** (geometry, grid).
>
> Slice 4 (implemented): the five ops with **no committed-demo user** moved out in one clean batch —
> **`Emitter`/`Instancer`/`Solids`** → `content-visual` (pure `operator_api`, no vendoring) and
> **`InstancesFromSignal`/`Particles3D`** → `content-3d` (reuse its vendored 3D headers). No demo change.
> (Correction to Slice 3's aside: the untracked demos are **gitignored by design** — `examples/demos/`
> `.gitignore` allowlists only the 6 committed demos; the rest are throwaway `.py`-generated projects,
> and the showcase ships as hosted video. So `Particles3D` moving is not a flagship regression.) Both
> packages verified: all their ops compile + register on install.
>
> Slice 5 (implemented — program complete, 12 ops moved out): `ShapeGrid` + `Lines` — the last two, and
> the only ones a committed demo uses. Rather than a shared package, each is pushed **into the demos that
> use it as a customizable project-local operator**: `geometry` carries both, `grid` carries `Lines`,
> `surge-lead` carries `ShapeGrid` (each a `.cpp` + manifest the demo owns and can specialize, compiled
> on load). The generalized standalone `examples/operators/{ShapeGrid,Lines}` were removed — the demos
> are now the examples. Verified render **parity** (geometry 0.317, grid 0.201, surge-lead 0.318 —
> unchanged). Tradeoff accepted: these three demos now compile-on-open (need the toolchain), where they
> were pure data before. **Result: the 12 recipe/single-look candidates are all out of the default
> install; the core catalog is the spine + composable primitives + dependency-pinned ops.**

The **native note-generator recipes** `Euclid`, `Chord`, `RandMelody` are recipes by ADR-0046, but
they are compiled *into the binary* (`builtin_audio_ops.cpp`), not dylibs, and the audio-package route
is less exercised. They are **recipe-by-policy** and a *later* move-out candidate — see Follow-ups —
not part of the first pass.

### Consequence for the numbers

The first pass moves **~12 pure visual ops** out of the two bundled core packages. Core visuals drop
from ~43 to ~31; the moved ops become two shipped example packages the app discovers the same way it
discovers `example-visuals` today. No user-facing capability is lost — the ops still exist, still load,
still appear in `list_operators` when their package is present; they are simply **not part of the
pre-built spine everyone pays for**.

## Alternatives Considered

- **Delete the recipes/content ops.** Rejected for the same reason ADR-0046 rejected it: they are
  useful, they preserve existing projects, and they are good first-run examples. The goal is to change
  *where they live*, not to remove them.

- **Keep everything bundled; rely on chooser ranking (ADR-0046 as-is).** Rejected as insufficient for
  *this* goal. Ranking makes recipes *quieter*, but a demoted-but-bundled recipe is still a finished
  node you *use* — not source you *open, read, and fork into your own operator*. It doesn't change the
  lesson the catalog teaches, and it never puts the operator's source in front of the user.

- **Ship the moved ops as example packages that are still copied into the bundle.** This is the
  weakest form and a reasonable *interim*: it removes them from the "core primitive" mental model and
  the chooser's front row without requiring the install-time compile. But it keeps the maintenance and
  bundle cost, so it does not fully serve "lean core." Recommended only as a migration waypoint.

- **Move the heavy ops (`Video`, `Webcam`, `Text`, `Model`) out too.** Rejected for now: the
  install-time compiler cannot link their dependencies (`package_compiler.cpp:157`), so they cannot be
  compile-at-load packages without first giving packages a link/dependency story. They stay core until
  that exists (Follow-ups).

## Consequences

- **Positive — the catalog teaches authoring.** What remains reads as primitives to compose and
  editable examples to fork; a moved-out op ships as **source a user opens, modifies, and makes their
  own** — the encouraged path in an LLM-empowered tool, where authoring an operator is a realistic
  default. The default surface is building blocks, matching ADR-0046's intent structurally, not just by
  ranking. (It also shrinks the per-op ADR-0042 DoD surface on the release path — a welcome side
  effect, not the point.)
- **Positive — dogfooded package path.** Our own recipes become the reference example packages,
  exercising the exact install/project-local flow ADR-0039 will lean on for community packages.
- **Positive — projects can carry their looks.** A project that wants `Particles3D` or a recipe can
  ship that op *in its folder* (`install_package(..., project_dir)`), consistent with "project
  operators ship with the project."
- **Tradeoff — first-load compile.** A moved op is compiled by `clang++` on install/first-launch,
  which requires the macOS Command Line Tools and adds a one-time compile step. Bundled ops have
  neither cost. Mitigation: ship the example packages *pre-compiled into `PlugIns/` for the default
  install* (the interim alternative above), while the *canonical source* lives in the example package
  — so a default user sees no regression and an advanced user/project gets the compilable source.
- **Tradeoff — discovery.** Ops that are not in the default install are harder to find. Mitigation:
  keep them registered-and-listed whenever their package is present, and document each recipe's
  primitive decomposition (ADR-0046 Migration step 3) in the example package's README.
- **Risk — existing projects.** A project referencing a moved op must have that op available. The
  loader already registers by descriptor name regardless of package origin, so as long as the default
  install still carries the ops (pre-compiled interim), old projects keep working. A truly minimal
  install that omits them needs a load-time "missing operator → offer to install package" cue
  (ADR-0019 posture; the missing-plugin cue already exists for audio plugins).

## Follow-ups

1. **Ratify the move-out list** with a per-op audit (ADR-0042 harness) so each candidate's
   decomposition-from-primitives is documented before it leaves core.
2. **Create `examples/recipes-visual` and `examples/content-visual` packages** with manifests; move
   the pure ops' sources there; decide bundle-pre-compile (interim) vs compile-at-load (target).
3. **Give packages a dependency/link story** so the heavy dependency-pinned ops (`Video`, `Text`,
   `Model`, `Webcam`) can move out of core too. Staged design in the **appendix below**; until it
   lands they are correctly core.
4. **Extend the policy to native audio recipes** (`Euclid`, `Chord`, `RandMelody`) once the audio
   package route is as exercised as the visual one.
5. **Add an audit check** that flags a new *pure recipe/single-look* op landing in a core package, so
   the catalog does not silently re-accrete (mirrors ADR-0046's "not only social enforcement").

## Appendix: External-Library Support for Packages (Follow-up #3)

### The constraint today

`PackageCompiler::compile_operator` (`app/src/packages/package_compiler.cpp`) emits **one** `clang++`
invocation with a fixed shape: `-std=c++17 -shared -fPIC`, `-I <operator_api>`, `-I <package_dir>`,
and — only when the op is `gpu` — the webgpu include plus `-L <lib_wgpu> -lwgpu_native -Wl,-rpath,…`.
It compiles a **single** source file (`op.source`) and links **nothing else**. There is no manifest
field for extra sources, include dirs, defines, libraries, or frameworks. That single-source,
wgpu-only-link shape is the entire reason the dependency-pinned ops can't be packages.

### The design axis: portable-by-source vs machine-specific-by-link

An external dependency a per-project op wants falls into one of four classes, which differ sharply in
whether the resulting dylib will *compile and load on a machine that is not the author's*:

| Class | Example (op) | Portable? | How it links |
|---|---|---|---|
| Header-only | `nlohmann_json` (`Model`, `MeshLoad`) | ✅ fully | already works — `#include` it; `-I <package_dir>` covers it |
| Vendored source | HAP `hap.c`+snappy (a `Video` variant) | ✅ fully | compile the extra `.c`/`.cpp`/`.mm` *into* the dylib |
| System framework | AVFoundation, CoreMedia (`Video`, `Webcam`) | ✅ on every mac | `-framework Foo` — present on all macOS |
| Prebuilt third-party lib | FreeType (`Text`), an arbitrary `.dylib` | ❌ machine-specific | `-L`/`-l` + rpath + ship the binary |

The blessed direction is **vendor the source** (portable) over **link a machine binary** (not). Note
the header-only row already works untouched — so the gap for `Model`/`MeshLoad` is only "let a package
vendor a header," not any linker feature.

### Staged proposal

**Stage 1 — vendored include dirs (portable, low-risk, unlocks the most). ✅ implemented.**
Landed as the classic `dependencies.vendor` block (parity with vivid-classic's proven system): a
package-level `dependencies: { vendor: [{ name, include }] }` list whose package-relative `include`
dirs are resolved, escape-guarded, and added as `-I` to every operator's compile
(`package_manifest.cpp` parses + fans onto `PackageOperator.include_dirs`; `package_compiler.cpp`
emits the flags; proven by `app/tests/test_package_vendor_include.cpp`). This unlocks every
header-only dependency — including `nlohmann_json` for `Model`/`MeshLoad`. The original sketch below
(a broader `sources`/`defines` shape) was superseded by the leaner classic schema; multi-source and
extra libraries move to the CMake escape hatch in Stage 2/3.

Grow `PackageOperator` (`package_manifest.h`) and the manifest:

```jsonc
{ "name": "MeshLoad", "kind": "gpu_visual",
  "sources": ["mesh_load.cpp"],             // was the single "source" field (kept as sugar for one)
  "include_dirs": ["vendor/json/include"],  // package-relative → -I
  "defines": ["MESHLOAD_STRICT=1"] }         // → -D
```

`compile_operator` loops these into `argv`. All paths resolve **package-relative and are rejected if
they escape the package dir** (no `../../..`). This covers every header-only lib and every
vendor-a-single-file-C/C++-lib case with **zero portability loss** — everything compiles from source
shipped inside the package. Moves `Model`, `MeshLoad`, `MeshRender`, `MeshDisplace` out of the
dependency-pinned set.

**Stage 2 — system frameworks + ObjC/ARC (macOS content ops).**

```jsonc
"frameworks": ["AVFoundation", "CoreMedia", "CoreVideo"],
"objc_arc": true   // set -fobjc-arc; allow .mm/.m sources
```

`-framework X` per entry; ARC/`.mm` handling per source (the bundled `Video`/`Webcam` CMake documents
the exact flag set each needs). Frameworks are portable *because they exist on every macOS install*,
but they widen what a package can reach (camera, mic, network) — so this stage rides the **trust gate**
(below). Unlocks `Video`, `Webcam`.

**Stage 3 — arbitrary prebuilt libraries (defer, gate hard).** `link_libraries` + `link_dirs`, the
vendored `.dylib` copied beside the operator plugin, rpath = `@loader_path`. This is what
`Text`/FreeType needs (a *built* lib). Least portable (a binary blob that must match arch/OS); the
manifest header already punts on it ("*no git/lockfile/vendor-deps — added later if needed*"). Prefer
building such deps from vendored source (Stage 1) over shipping a binary; design this last.

### Cross-cutting

- **Trust — be precise about what is actually new.** The compile route *already* runs arbitrary C++ on
  install, so external libraries are **not** a new code-execution boundary. What Stage 2/3 add is
  **capability reach** — a framework link is how a package touches camera/mic/network. Install-time
  consent should therefore **name the frameworks/libs** a package pulls in, not just say "compiling…"
  (consistent with ADR-0031's trust posture and ADR-0039's registry-trust future).
- **Portability as policy.** Stage 1 (vendored source) and Stage 2 (system frameworks) are portable and
  blessed; Stage 3 (prebuilt libs) may fail to load elsewhere and must produce a loud, *named*
  load-time error (ADR-0019), never a silent skip.
- **Per-platform manifest blocks.** Frameworks are macOS-only; a `link` block should key by platform
  (`"macos": { "frameworks": […] }`) so a package degrades on Linux/Windows the way bundled ops do.
- **rpath/loader.** Any linked dylib must resolve at load — mirror the existing wgpu `-Wl,-rpath,…`,
  pointing at the package/plugin dir, and copy vendored binaries beside the operator `.dylib`.

### Recommendation

Land **Stage 1 now** — a small, additive change to `PackageOperator` + `compile_operator`, fully
portable, and it immediately unblocks the glTF/mesh family so those ops can leave core under this ADR.
Design **Stage 2 behind the capability-naming trust gate** to unblock `Video`/`Webcam`. Keep **Stage 3
deferred**, preferring vendored source over shipping prebuilt binaries.

## References

- ADR-0046: Operators Are Composable Primitives First — the primitive/recipe split this ADR acts on.
- ADR-0042: Operator Audit and a Per-Operator Definition of Done — the per-op cost that motivates leanness.
- ADR-0039: Community Packages Are "Coming Soon" Until Registry Trust Exists — the package future.
- ADR-0053: Audio Reactivity Is Explicit Graph Nodes — the reactivity spine kept in core.
- ADR-0021 / ADR-0016: content is browsable; shaders are content — the CustomShader spine.
- ADR-0019: Nothing Fails Silently — the missing-operator cue posture.
- Code: `app/operators/CMakeLists.txt` (bundled build), `app/operators/packages/example-visuals/`
  and `example-audio/` (the example-package template), `app/src/packages/package_compiler.cpp`
  (install-time compile, wgpu-only link), `app/src/packages/package_manager.h` (project-local
  `out_dir`), `app/src/main.cpp` (startup `PlugIns/` scan + project package discovery).
