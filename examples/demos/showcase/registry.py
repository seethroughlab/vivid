"""The curated showcase registry — one entry per ADR-0037 showcase type.

ADR-0037 ("showcase demos gate the real website") requires that every website hero image come from
a refreshed, regenerable, loadable, screenshottable saved project, covering five showcase types:

  1. first-run beginner project (matches ADR-0035)
  2. scene/clip project using Session View + the visual graph together
  3. audio-reactive visual project that makes the mapping bridge inspectable
  4. creative-coding project that forks/authors a shader or project-local operator
  5. plugin-based music project on the curated free-plugin path (Surge XT, ADR-0036)

Each entry points at either a demo builder module (importable `build(v, save=True)`) or a tutorial
`build.py` (standalone, run via subprocess), plus the saved project dir the harness loads and the
prerequisites it declares. Data only — no I/O, no app calls.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path

# repo root: registry.py -> showcase -> demos -> examples -> <repo>
REPO = Path(__file__).resolve().parents[3]
DEMO_PROJECTS = REPO / "examples" / "demos" / "projects"
TUTORIALS = REPO / "examples" / "tutorials"


class Kind(Enum):
    DEMO = "demo"          # importable module with build(v, save=True)
    TUTORIAL = "tutorial"  # standalone build.py, run via subprocess


class Prereq(Enum):
    SURGE = "surge"               # Surge XT.clap present (curated free CLAP synth)
    CASSETTE_DRUMS = "cassette"   # Cassette Drums.vst3 present (free drum machine)
    CLANG = "clang"               # xcode CLT clang++ for compiled project operators


# ADR-0037 showcase type -> human label (for coverage reporting).
ADR0037_TYPES = {
    1: "first-run beginner project",
    2: "Session View + visual graph",
    3: "audio-reactive mapping bridge",
    4: "creative-coding (shader / project-local operator)",
    5: "plugin-based music (curated free plugin)",
}


@dataclass(frozen=True)
class Showcase:
    id: str                             # stable slug -> hero filename + report name
    adr0037_type: int                   # 1..5, which showcase type this satisfies
    kind: Kind
    title: str                          # human label for the index
    target: str                         # DEMO: importable module name; TUTORIAL: build.py path rel to REPO
    project_dir: Path                   # saved artifact dir to load_project() after regen
    prereqs: tuple[Prereq, ...] = ()    # capabilities that gate the FULL experience (missing -> WARN)
    wants_audio: bool = False           # eligible for --audio (has audible clips/transport)
    hero: str = ""                      # hero PNG filename (defaults to "<id>.png")
    video_scene: int = 0                # scene to launch for hero/video capture (multi-section songs)
    notes: str = ""

    def hero_name(self) -> str:
        return self.hero or f"{self.id}.png"

    def video_name(self) -> str:
        """Showcase clip filename (parallel to the hero PNG). The site poster is the hero PNG."""
        return f"{self.id}.mp4"

    def target_path(self) -> Path:
        """Absolute path of the tutorial build.py (TUTORIAL only)."""
        return REPO / self.target


SHOWCASES: list[Showcase] = [
    Showcase(
        id="first-project",
        adr0037_type=1,
        kind=Kind.TUTORIAL,
        title="First MCP-native creative-coding project (beginner)",
        target="examples/tutorials/mcp-native-first-project/build.py",
        project_dir=TUTORIALS / "mcp-native-first-project" / "project",
        prereqs=(Prereq.SURGE,),
        wants_audio=True,
        notes="ADR-0035 golden path A; Surge XT beginner instrument + project-local shader.",
    ),
    Showcase(
        id="pulse-song",
        adr0037_type=2,
        kind=Kind.DEMO,
        title="Pulse — techno song (Session View + visual graph)",
        target="pulse",
        project_dir=DEMO_PROJECTS / "pulse",
        prereqs=(Prereq.SURGE, Prereq.CASSETTE_DRUMS),
        wants_audio=True,
        notes="Multi-track scene + reactive geometry graph; Surge voices + free Cassette Drums.",
    ),
    Showcase(
        id="mirror-bridge",
        adr0037_type=3,
        kind=Kind.DEMO,
        title="Mirror — the bidirectional audio<->visual bridge",
        target="mirror",
        project_dir=DEMO_PROJECTS / "mirror",
        prereqs=(Prereq.SURGE, Prereq.CASSETTE_DRUMS),
        wants_audio=True,
        video_scene=2,   # the CHORUS — the full, busiest section (intro/scene-0 is deliberately sparse)
        notes="viz.feedback->cutoff, viz.blur->resonance return leg; makes the bridge inspectable.",
    ),
    Showcase(
        id="shader-edit",
        adr0037_type=4,
        kind=Kind.TUTORIAL,
        title="Live shader edit — creative-coding a project-local shader",
        target="examples/tutorials/live-shader-edit/build.py",
        project_dir=TUTORIALS / "live-shader-edit" / "project",
        prereqs=(),
        wants_audio=False,
        notes="Self-contained; no plugin/clang prerequisite, so it always gates green on any machine.",
    ),
    Showcase(
        id="neon-song",
        adr0037_type=5,
        kind=Kind.DEMO,
        title="Neon — synthwave song on the curated free-plugin path",
        target="neon",
        project_dir=DEMO_PROJECTS / "neon",
        prereqs=(Prereq.SURGE, Prereq.CASSETTE_DRUMS),
        wants_audio=True,
        notes="Surge XT arp + bass + free Cassette Drums; the ADR-0036 free-plugin music showcase.",
    ),
]


def by_id(showcase_id: str) -> Showcase | None:
    return next((s for s in SHOWCASES if s.id == showcase_id), None)


def by_type(adr0037_type: int) -> list[Showcase]:
    return [s for s in SHOWCASES if s.adr0037_type == adr0037_type]


def select(ids: list[str] | None = None, adr0037_type: int | None = None) -> list[Showcase]:
    """Filter the registry by explicit ids and/or ADR-0037 type. Unknown ids raise (fail fast)."""
    shows = SHOWCASES
    if adr0037_type is not None:
        shows = [s for s in shows if s.adr0037_type == adr0037_type]
    if ids:
        known = {s.id for s in SHOWCASES}
        unknown = [i for i in ids if i not in known]
        if unknown:
            raise ValueError(f"unknown showcase id(s): {', '.join(unknown)}; "
                             f"known: {', '.join(sorted(known))}")
        wanted = set(ids)
        shows = [s for s in shows if s.id in wanted]
    return shows


def missing_types() -> list[int]:
    """ADR-0037 types (1..5) not covered by any registry entry."""
    covered = {s.adr0037_type for s in SHOWCASES}
    return [t for t in ADR0037_TYPES if t not in covered]
