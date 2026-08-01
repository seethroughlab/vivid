# Phase 6: Tests, Tooling, CI, And Release Infrastructure

Status: done (audited 2026-07-31)

## Verdict

**FAIL — 2×P1, + 4×P2, 4×P3.** The test/CI foundation is real and mostly honest: 6
workflows, a production gate with a genuine **min-test guard** (`too_few_tests` → fail,
closing the "green run exercises too little" mode) on the core path, ASan/UBSan/TSan/audio-
TSan legs on every PR, a version guard enforcing tag==CMake, and a sign/notarize script
that fails loudly on a missing signing identity. **But** two release-integrity gaps block a
*trustworthy* first release: the **release-tag pipeline runs none of the sanitizer or
audio-engine legs** before shipping (a direct tag ships unverified for exactly the
highest-risk RT paths, P1-01), and the notarization step **silently produces a
signed-but-un-notarized DMG** when creds are absent (P1-02) — while the site promises
"notarized." Both are my P1 assessment; **flagging for your call on RC-blocking status.**

## Purpose

Verify that the codebase has enough automated evidence and release tooling to support a confident
first release.

## User Task

Run the release candidate through local gates, CI expectations, version checks, packaging scripts,
and documented release steps without relying on private maintainer memory.

## Hypothesis

If tooling is release-ready, a maintainer can reproduce the release decision and understand which
parts are verified versus scaffolded.

## Pressure Test

Run or inspect production gates, test labels, sanitizer targets, release scripts, GitHub Actions,
version guard, appcast generation, signing/notarization scaffolding, and documentation.

## Scope

- CMake build targets, local test commands, production gate, test labels, sanitizer builds, release
  scripts, GitHub Actions, version guard, appcast generation, signing/notarization scripts, docs, and
  artifact layout.
- The difference between verified automation and intentionally scaffolded release infrastructure.

Out of scope: provisioning external secrets or runners unless the audit owner has access.

## Audit Procedure

1. Inventory every documented release and test command, then mark it verified, scaffolded, stale, or
   missing.
2. Run local production-gate and focused tests where practical. Record command, build directory,
   result, duration, and notable warnings.
3. Inspect CI workflows for parity with local commands, required secrets, artifact names, and failure
   behavior.
4. Review release scripts for credential checks, partial artifact cleanup, version/tag consistency,
   appcast correctness, and honest failure messages.
5. Map P0/P1 findings from code phases to automated coverage or a documented manual release check.

## Evidence To Collect

- Command inventory with status and output summaries.
- Production-gate report path and status.
- CI workflow matrix with runner, trigger, secrets, artifacts, and current verification status.
- Release script checklist with missing credential and partial-failure behavior.
- Coverage map for critical release risks.

## Deliverables

- Release infrastructure readiness report.
- Required pre-release command list.
- CI/local parity findings.
- Test gap list tied to release risks.

## Acceptance Criteria

- The production gate has a clear pass/fail interpretation and minimum-test guard.
- Critical code paths have tests proportional to release risk.
- CI and local commands are documented and agree.
- Release scripts fail loudly on missing credentials or partial artifacts.
- Scaffolded release infrastructure is clearly labeled until verified.

## Failure Modes

- A green test run silently exercises too little of the product.
- Local and CI release paths use different assumptions.
- Release scripts produce unsigned, incomplete, or mislabeled artifacts.
- Documentation hides known infrastructure gaps.

## Evidence Log

Method: inventory of `.github/workflows/`, `scripts/`, `tools/`, the CMake gate, and the
release runbook; the two P1 findings re-read directly. No repo branch-protection/ruleset
file exists in-tree, so "required check" status is reconstructed from ADR intent, not an
enforceable config.

### A. CI workflows (6)

- **`headless-tests.yml`** (ubuntu, push+PR): matrix `VIVID_SANITIZE=OFF/ON` full `ctest`,
  plus a `thread-sanitizer` job (`build-tsan`, `ctest -L THREAD`, `halt_on_error=1`). →
  checks `tests (sanitize=OFF/ON)`, `tests (thread-sanitizer)`.
- **`production-gate-pr.yml`** (self-hosted macOS, PR): `gate` (`run_production_gate.sh
  core`, uploads `reports/`, PR comment), `audio-engine-tests` (`ctest -L AUDIO_ENGINE -E
  test_session_concurrency`), `audio-thread-sanitizer` (`ctest -L AUDIO_THREAD`).
- **`version-guard.yml`** (ubuntu): `check_version.py` (+`--expect` on tags) + MCP↔control
  parity test.
- **`release-macos.yml`** (self-hosted, tags+dispatch): version-guard → clean Release build
  → **`run_production_gate.sh core` only** → sign/notarize → appcast → publish. Header
  self-labels "UNVERIFIED SCAFFOLD."
- **`pages.yml`** (site deploy) and **`llm-mcp-evals.yml`** (manual, informational) — not
  code gates.

Reconstructed required set (ADR-0029): `gate`, `audio-engine-tests`, `check`, `tests
(sanitize=OFF)`, `tests (sanitize=ON)`, `tests (thread-sanitizer)`. Notes: only 2 of 6 are
self-hosted (the "6-check self-hosted gate" label is imprecise, → P3); a 7th check
(`audio-thread-sanitizer`) runs on PRs but isn't in the documented required set; nothing
in-tree enforces any of it.

### B. Production gate

`scripts/run_production_gate.sh` (label-filtered `ctest` by profile; only `core` =
HEADLESS_SMOKE has tests) + `tools/production_gate_report.py` + `_budgets.toml`. **Min-test
guard confirmed:** `min_tests=10` → `too_few_tests` error → `fail`; `max_failures=0`;
`max_duration=120s` (→ `degraded`, `--strict` blocks). Self-test runs first. This closes the
silent-empty mode **for the core path only** — the `audio-engine`/TSan legs run raw `ctest`
with no min-test/budget judging (→ P2-01). `min_tests=10` is also low vs the ~61
HEADLESS_SMOKE tests present (catches near-emptiness, not a large partial regression).

### C. Label → CI mapping

`HEADLESS_SMOKE` (default) → `gate` + release gate; `THREAD` → `thread-sanitizer`;
`AUDIO_ENGINE` → `audio-engine-tests`; `AUDIO_THREAD` (`test_session_concurrency`) →
`audio-thread-sanitizer`. All sanitizer/audio legs are **PR-only** — the release tag
pipeline invokes none of them (→ P1-01).

### D. Coverage map — highest-risk findings from Phases 1–5 vs automated evidence

| Finding | Test today | In release pipeline? |
|---|---|---|
| Ph2 P0-01 plugin RT crash (fixed #190) | `test_plugin_crash_attrib` (HEADLESS_SMOKE) + audio-TSan leg (PR) | **No** (P1-01) |
| Ph2 P2-01 CLAP param-queue torn read | none | — |
| Ph4 P1-01 undo plugin-window UAF | none | — |
| Ph4 P1-02 missing-op param data-loss | none | — |
| Ph4 P1-03 save/load round-trip | none (the gap itself) | — |
| Ph3 P2s (device-loss, blank-vs-empty) | none | — |

So the highest-risk paths from Phases 2–4 are **largely uncovered by CI**, and even the one
that is (P0-01) is absent from the release pipeline. This is the central Phase-6 conclusion:
the gate is honest about *what it runs*, but *what it runs* doesn't yet reach the release-
grade risks the code phases surfaced.

### E. Findings

#### P1-01: The release-tag pipeline runs no sanitizer or audio-engine verification

- Surface: `.github/workflows/release-macos.yml`
- Impact: before sign/notarize/publish, the release job runs only `check_version.py` +
  `run_production_gate.sh core` (HEADLESS_SMOKE) — **no** `VIVID_SANITIZE`, no `-L
  THREAD/AUDIO_THREAD/AUDIO_ENGINE`. A tag pushed directly (or a release cut without the
  full PR-check history) ships with **zero** TSan/ASan/audio verification — exactly the legs
  that cover the RT-safety paths (Phase 2's P0/P2). Release integrity relies on the
  assumption that PR checks already ran, which the workflow does not assert.
- Evidence: `release-macos.yml:38,50,78,86` (only check_version + gate core before sign);
  contrast the PR/headless legs in §A.
- Smallest acceptable fix: the release job runs the sanitizer + audio-engine legs (or
  depends on their green status for the tagged SHA) before signing.
- Owner/status: Unassigned | P1 (candidate RC-process blocker) | own PR

#### P1-02: Missing notarization creds silently ship a signed-but-un-notarized DMG

- Surface: `scripts/release/sign_and_notarize.sh`
- Impact: only `APPLE_CODESIGN_IDENTITY` fails loudly; with notarization creds absent the
  script sets `NOTARIZE="none"`, prints "DMG will be signed but not notarized," **skips the
  `spctl`/`stapler validate` checks**, and exits 0 with an identically-named DMG — which the
  CI publish step would ship. The site promises a "code-signed and notarized" build that
  "opens without a Gatekeeper detour," so a silent un-notarized ship = a broken first-run
  for the flagship download. Violates "release scripts fail loudly on missing credentials or
  partial artifacts."
- Evidence: `sign_and_notarize.sh:38` (`NOTARIZE="none"` default), `:40-43` (only set if
  creds), `:94` (skip message, no exit), `:112-116` (verification skipped when un-notarized).
- Smallest acceptable fix: on a tag/CI build, require notarization — fail if creds are
  absent — and gate publish on a passing `spctl --assess`/`stapler validate`.
- Owner/status: Unassigned | P1 (candidate RC-process blocker) | own PR

#### P2-01: The min-test guard only protects the core path

`audio-engine-tests` and both TSan legs run raw `ctest -L …` with no min-test/budget
judging; a label that matches nothing passes silently. Fix: wrap them in the same report
tool or add a per-job expected-count check. Owner/status: Unassigned | P2.

#### P2-02: Auto-update is a no-op stub; the appcast is unsigned

`sparkle_bridge_stub.cpp:9-11` returns `available=false` / no-op with **zero callers** in
`app/src`; CI never passes `--ed-signature` so the published appcast has no Sparkle
signature (`release-macos.yml:86-90`, `generate_appcast.py:33`). Auto-update is
non-functional for first release. Fix: either wire Sparkle + sign the appcast, or **document
auto-update as explicitly deferred** (answers Open Question). Owner/status: Unassigned | P2
(or waive-as-deferred).

#### P2-03: Release status is mislabeled (doc/ADR contradiction)

`release-macos.yml:1` and `docs/release/README.md:3,17-20` call the sign/notarize pipeline
"unverified scaffold," while `ADR-0040:274-293` states it was exercised and produced a
signed+notarized DMG installed to `/Applications`. A maintainer cannot tell what is real —
the inverse of the usual risk but the same violation of "scaffolded infra clearly labeled
until verified." Fix: reconcile to one truth. Owner/status: Unassigned | P2.

#### P2-04: CI/local parity drift

The docs' build command is a full-app default-config build, unlike every CI leg
(RelWithDebInfo, `VIVID_BUILD_APP=OFF`); the sanitizer/audio recipes and their build dirs
are undocumented, and build-dir names are inconsistent across CI files (`app/build` vs
top-level `build`; `app/build-tsan-audio` vs `build-tsan`). A developer following the docs
cannot reproduce 4 of the 6 required checks. Fix: document the sanitizer/audio recipes and
normalize build-dir names. Owner/status: Unassigned | P2.

#### P3-01: Version guard has no monotonicity check

`check_version.py` asserts tag==CMake version but never that the tag exceeds the last
release, and doesn't validate appcast/tag consistency beyond reusing `GITHUB_REF_NAME`. A
reused/backward version passes. Owner/status: Unassigned | P3.

#### P3-02: No partial-artifact cleanup on sign/notarize failure

The app-zip is removed only on the success path (`sign_and_notarize.sh:92`); a mid-flow
failure leaves partial artifacts in `OUT_DIR`. Owner/status: Unassigned | P3.

#### P3-03: "6-check self-hosted gate" label is imprecise; required checks unenforced in-tree

Only 2 of 6 checks are self-hosted; a 7th (`audio-thread-sanitizer`) runs but isn't
documented as required; branch protection is a repo-settings toggle not present in the tree.
Fix: correct the description; capture the required-set in a checked-in note. Owner/status:
Unassigned | P3.

#### P3-04: Stale local dist artifacts

`build/dist/vivid_poc.{dmg,zip}` (Jun 30 POC) are not outputs of the documented `vivid.dmg`
pipeline. Fix: remove/ignore. Owner/status: Unassigned | P3.

## Open Questions (answered / flagged)

- **Which build dir/config defines the RC?** `release-macos.yml` does a clean `rm -rf
  app/build` → `-DCMAKE_BUILD_TYPE=Release` full-app build (`:45-47`) — distinct from every
  PR leg (RelWithDebInfo, app-OFF) and from the docs' plain command (P2-04). Recommend
  documenting `app/build` Release as the canonical RC and reconciling the doc command.
- **Which CI workflows are required before tagging?** Per ADR-0029 the required set is
  `gate`, `audio-engine-tests`, `check`, and the three `tests (…)` legs — but this is
  enforced by a repo-settings toggle not present in-tree, and the release *workflow itself*
  runs none of the sanitizer/audio legs (P1-01). Recommend the release job re-verify them.
- **Are signing/notarization/appcast/updater required for the first build or deferred?**
  Signing + notarization are **required and (per ADR-0040) exercised** — but the script can
  silently ship un-notarized (P1-02) and the status is mislabeled (P2-03). The appcast +
  Sparkle updater are **not functional** (unsigned appcast, no-op stub, zero callers) —
  recommend explicitly **deferring auto-update** for first release and saying so, rather than
  shipping a stub that implies it works (P2-02).

## Follow-Up Plans

- **P1-01 fix (candidate RC-process blocker):** the release job runs/depends on the
  sanitizer + audio-engine legs for the tagged SHA before signing.
- **P1-02 fix (candidate RC-process blocker):** on a tag/CI build, require notarization
  (fail on missing creds) and gate publish on `spctl`/`stapler validate`.
- P2 fixes: min-test guard on the audio/TSan legs (P2-01); decide + document auto-update
  (P2-02); reconcile the scaffold-vs-exercised status (P2-03); document sanitizer/audio
  recipes + normalize build-dir names (P2-04).
- P3: version monotonicity check, partial-artifact cleanup, correct the "6-check
  self-hosted" description + check-in the required set, drop stale `build/dist` POC artifacts.
- **Coverage debt (from §D):** the P1s from Phases 2 & 4 need tests before RC — a plugin-
  window-UAF-on-undo regression test (Ph4 P1-01), a degraded-project save/load test (Ph4
  P1-02), a golden round-trip test (Ph4 P1-03), and a CLAP param-queue race test (Ph2 P2-01).
  Each should carry the label that puts it in the gate/PR legs.
