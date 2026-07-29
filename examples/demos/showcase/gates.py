"""Pure verdict logic for the showcase QA harness — no I/O, no app calls.

`evaluate()` turns a bag of raw control-server step responses into a PASS/WARN/FAIL verdict per
ADR-0037, and `build_index()` rolls per-showcase results into the machine-readable gate the website
build / release checklist reads. Kept pure so it is unit-testable against recorded step dicts.

ADR-0037 PASS definition (all must hold):
  1. non-blank hero      — capture.captured AND not capture.is_blank (cross-checked with analyze)
  2. structurally valid  — validate_project.valid is True
  3. not degraded        — validate_project.degraded is False (loaded-but-broken plugin/shader)
  4. quality not failing — run_quality_check overall != "fail"
Anything less than PASS but not a hard failure is WARN (regenerable + loadable, attention needed).
"""
from __future__ import annotations

from dataclasses import dataclass, field

# A hero that only rendered non-blank after this many capture attempts warmed suspiciously slowly.
WARN_WARMUP_ATTEMPTS = 4


@dataclass
class ShowcaseResult:
    id: str
    adr0037_type: int
    title: str
    kind: str
    target: str
    gate: str = "fail"                       # "pass" | "warn" | "fail"
    reasons: list[str] = field(default_factory=list)   # human gate reasons (fail reasons first)
    hero_path: str | None = None
    captured: bool = False
    is_blank: bool | None = None
    warm_attempts: int | None = None
    image_stats: dict = field(default_factory=dict)     # brightness/contrast/activity/hash/...
    valid: bool | None = None
    degraded: bool | None = None
    validate_issues: list[dict] = field(default_factory=list)
    health_severity: str | None = None
    quality_overall: str | None = None
    quality_checks: list[dict] = field(default_factory=list)
    prereqs_missing: list[str] = field(default_factory=list)
    steps: dict = field(default_factory=dict)           # raw step responses (for debugging)
    # optional AV clip (only when --video ran); WARN-only, never gates the release
    video_path: str | None = None
    video_captured: bool = False
    video_frames: int | None = None
    video_motion: float | None = None


def prereqs_missing(showcase, caps: dict) -> list[str]:
    """Declared prereqs of `showcase` that this machine lacks (per the capability probe)."""
    missing = []
    for p in showcase.prereqs:
        if not caps.get(p.value, False):
            missing.append(p.value)
    return missing


def _issue_brief(validate: dict, level: str) -> str:
    hits = [i.get("issue", "") for i in validate.get("issues", []) if i.get("level") == level]
    return "; ".join(h for h in hits if h)


def evaluate(showcase, steps: dict, caps: dict) -> ShowcaseResult:
    fail: list[str] = []
    warn: list[str] = []

    res = ShowcaseResult(
        id=showcase.id, adr0037_type=showcase.adr0037_type, title=showcase.title,
        kind=showcase.kind.value, target=showcase.target, steps=steps,
    )
    res.prereqs_missing = prereqs_missing(showcase, caps)

    # --- regenerate ---------------------------------------------------------------------------
    regen = steps.get("regen")
    if isinstance(regen, dict) and regen.get("ok") is False:
        detail = regen.get("error") or f"returncode={regen.get('returncode')}"
        fail.append(f"regenerate failed: {detail}")

    # --- load the portable artifact -----------------------------------------------------------
    # call_optional returns None on a control-server error, so a non-None load == ok.
    if steps.get("load") is None:
        fail.append("load_project failed (the saved portable artifact did not load)")

    # --- hero capture (+ analyze cross-check) --------------------------------------------------
    capture = steps.get("capture") or {}
    analyze = (steps.get("analyze") or {}).get("analysis") or {}
    res.captured = bool(capture.get("captured"))
    res.is_blank = capture.get("is_blank")
    res.warm_attempts = capture.get("warm_attempts")
    res.hero_path = capture.get("path")
    res.image_stats = {k: analyze.get(k) for k in (
        "brightness", "contrast", "activity", "color_spread", "dominant_colors", "hash",
        "width", "height", "blank_reason") if k in analyze}
    analyze_blank = analyze.get("is_blank")
    if not res.captured:
        fail.append("hero not captured (nothing feeds the Output node)")
    elif res.is_blank or analyze_blank is True:
        fail.append(f"hero is blank ({analyze.get('blank_reason') or 'near-black/near-uniform'})")
    elif res.warm_attempts and res.warm_attempts >= WARN_WARMUP_ATTEMPTS:
        warn.append(f"hero warmed slowly ({res.warm_attempts} capture attempts)")

    # --- optional AV clip (only when --video ran; WARN-only — the clip is a website nicety, not
    # part of the ADR-0037 release gate) --------------------------------------------------------
    video = steps.get("video")
    if isinstance(video, dict):
        vstatus = video.get("status") or {}
        motion = video.get("motion") or {}
        res.video_captured = bool(video.get("ok"))
        res.video_path = video.get("path")
        res.video_frames = vstatus.get("frames")
        res.video_motion = motion.get("motion_score")
        if not res.video_captured:
            warn.append("video clip not captured")
        elif motion.get("is_moving") is False:
            warn.append("video clip shows little motion (is_moving=false)")

    # --- validate_project (valid AND not degraded) --------------------------------------------
    validate = steps.get("validate")
    if isinstance(validate, dict):
        res.valid = validate.get("valid")
        res.degraded = validate.get("degraded")
        res.validate_issues = validate.get("issues", [])
        if res.valid is False:
            brief = _issue_brief(validate, "error") or validate.get("summary", "")
            fail.append(f"project invalid: {brief}")
        if res.degraded:
            brief = _issue_brief(validate, "error") or _issue_brief(validate, "warning")
            warn.append(f"project degraded{': ' + brief if brief else ''}")
    else:
        warn.append("validate_project unavailable")

    # --- runtime health -----------------------------------------------------------------------
    health = (steps.get("health") or {}).get("health") or {}
    res.health_severity = health.get("severity")
    if res.health_severity and res.health_severity != "ok":
        warn.append(f"health severity={res.health_severity}")

    # --- quality checks -----------------------------------------------------------------------
    quality = steps.get("quality") or {}
    res.quality_overall = quality.get("overall")
    res.quality_checks = quality.get("checks", [])
    if res.quality_overall == "fail":
        fail.append("quality overall=fail")
    elif res.quality_overall == "warn":
        warn.append("quality overall=warn")

    # --- optional audio leg (only when --audio ran it) ----------------------------------------
    audio = steps.get("audio")
    if isinstance(audio, dict):
        st = (audio.get("check") or {}).get("status")
        if st == "fail":
            fail.append("audio clipping (no_audio_clipping=fail)")
        elif st == "warn":
            warn.append("audio check warn (transport not playing?)")

    # --- declared prereqs missing on this machine (cap at WARN, never FAIL) --------------------
    if res.prereqs_missing:
        warn.append("missing prereqs: " + ", ".join(res.prereqs_missing))

    res.reasons = fail + warn
    res.gate = "fail" if fail else ("warn" if warn else "pass")
    return res


def build_index(results: list[ShowcaseResult], caps: dict, missing_types: list[int] | None = None) -> dict:
    """Roll per-showcase results into the release-checklist gate artifact."""
    from . import registry  # local import to keep this module import-light for tests

    counts = {"pass": 0, "warn": 0, "fail": 0}
    for r in results:
        counts[r.gate] = counts.get(r.gate, 0) + 1

    # ADR-0037 coverage: worst gate among showcases covering each type; "missing" if none ran.
    rank = {"pass": 0, "warn": 1, "fail": 2}
    coverage: dict[str, str] = {}
    for t in registry.ADR0037_TYPES:
        got = [r for r in results if r.adr0037_type == t]
        coverage[str(t)] = max((r.gate for r in got), key=lambda g: rank[g]) if got else "missing"

    any_missing = (missing_types or []) or [t for t, g in coverage.items() if g == "missing"]
    if counts["fail"]:
        gate = "fail"
    elif counts["warn"] or any_missing:
        gate = "pass_with_warnings"
    else:
        gate = "pass"

    return {
        "capabilities": caps,
        "adr0037_coverage": coverage,
        "gate": gate,
        "counts": counts,
        "showcases": [
            {
                "id": r.id, "type": r.adr0037_type, "gate": r.gate, "title": r.title,
                "hero": r.hero_path, "reasons": r.reasons,
                "brightness": r.image_stats.get("brightness"),
                "hash": r.image_stats.get("hash"),
            }
            for r in results
        ],
    }
