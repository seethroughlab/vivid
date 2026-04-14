# Beta Readiness Checklist

Status: **Phase 1 complete, Phases 2-6 pending**

Columns:
- **Result**: PASS / FAIL / SKIP / PENDING
- **Blocking?**: YES / NO / CONDITIONAL (depends on beta path exposure)
- **Evidence**: test name, log path, screenshot, or artifact link
- **Follow-up**: issue/task ID or "none"

---

## Automated Baseline (Phase 2)

| Area | Item | Result | Blocking? | Evidence | Follow-up |
|------|------|--------|-----------|----------|-----------|
| Build | Debug build succeeds | PENDING | YES | | |
| Build | RelWithDebInfo build succeeds | PENDING | YES | | |
| Test | CTest full baseline passes | PENDING | YES | | |
| Test | test_demo_graphs — all sample graphs | PENDING | YES | | |
| Test | UI_SMOKE | PENDING | YES | | |
| Test | GUI_SMOKE (windowed editor flows) | PENDING | YES | | |
| Test | GUI_ENV (package-dependent) | PENDING | CONDITIONAL | | |
| Test | Movie playback automated gate (4 tests) | PENDING | YES | | |
| Test | Movie playback runtime diagnostics | PENDING | YES | | |
| Test | Stability stress suite (4 tests) | PENDING | YES | | |
| Test | Phase 6 soak (extended run) | PENDING | YES | | |

## Sample Graph A/V Review (Phase 3)

| Area | Item | Result | Blocking? | Evidence | Follow-up |
|------|------|--------|-----------|----------|-----------|
| intro/ | All intro graphs pass A/V review | PENDING | YES | | |
| audio/ | All audio graphs pass A/V review | PENDING | YES | | |
| gpu/ | All GPU graphs pass A/V review | PENDING | YES | | |
| filters/ | All filter graphs pass A/V review | PENDING | YES | | |
| media/ | Movie-file and file-backed media graphs (env-dependent noted) | PENDING | CONDITIONAL | | |
| io/ | Live I/O graphs: MIDI, OSC, and Syphon (env-dependent noted) | PENDING | CONDITIONAL | | |
| reference_graphs/ | Reference graphs pass non-onboarding smoke/A/V review | PENDING | CONDITIONAL | | |
| tests/graphs/ | Listening and parity fixtures pass fixture review | PENDING | CONDITIONAL | | |
| Summary | No scary audio in any starter graph | PENDING | YES | | |
| Summary | No black/silent output in any starter graph | PENDING | YES | | |

## Operator Inspector Review (Phase 4)

| Area | Item | Result | Blocking? | Evidence | Follow-up |
|------|------|--------|-----------|----------|-----------|
| Audio | All audio operator inspectors reviewed | PENDING | YES | | |
| Control | All control operator inspectors reviewed | PENDING | YES | | |
| GPU | All GPU operator inspectors reviewed | PENDING | YES | | |
| WGSL | All WGSL filter inspectors reviewed | PENDING | YES | | |
| Summary | No clipped/overlapping controls | PENDING | YES | | |
| Summary | No dangerous default values | PENDING | YES | | |
| Summary | No programmer-only labels in beginner path | PENDING | YES | | |

## Beginner Docs Review (Phase 5)

| Area | Item | Result | Blocking? | Evidence | Follow-up |
|------|------|--------|-----------|----------|-----------|
| Docs | README.md reviewed | PENDING | YES | | |
| Docs | GETTING-STARTED.md reviewed | PENDING | YES | | |
| Docs | graphs/README.md reviewed | PENDING | YES | | |
| Docs | graphs/intro/README.md reviewed | PENDING | YES | | |
| Docs | "First 15 Minutes" path exists and works | PENDING | YES | | |
| Docs | macOS setup with Homebrew/CMake is clear | PENDING | YES | | |
| Docs | No developer-first language in beginner path | PENDING | YES | | |
| Docs | No private names or internal references leaked | PENDING | YES | | |
| Docs | Fallback guidance for missing devices/permissions | PENDING | YES | | |

## Install and Pilot (Phase 6)

| Area | Item | Result | Blocking? | Evidence | Follow-up |
|------|------|--------|-----------|----------|-----------|
| Install | Clean macOS setup succeeds | PENDING | YES | | |
| Install | Homebrew/CMake prerequisites documented and work | PENDING | YES | | |
| Permissions | Audio output works on first launch | PENDING | YES | | |
| Permissions | Microphone permission path is friendly | PENDING | CONDITIONAL | | |
| Permissions | Camera permission path is friendly | PENDING | CONDITIONAL | | |
| Permissions | MIDI/IAC permission path is friendly | PENDING | CONDITIONAL | | |
| UX | Example browser: beginner examples findable | PENDING | YES | | |
| UX | Example browser: package-dependent graphs labeled | PENDING | YES | | |
| UX | Example browser: env-dependent graphs not mistaken for starters | PENDING | YES | | |
| UX | Friendly errors when devices/deps are absent | PENDING | YES | | |
| Pilot | Internal full pass complete | PENDING | YES | | |
| Pilot | One trusted external tester pilot complete | PENDING | YES | | |
| Pilot | Known limitations note written | PENDING | YES | | |
| Ship | Final go/no-go decision | PENDING | YES | | |
