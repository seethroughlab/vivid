#!/usr/bin/env python3
"""Vivid 4 website static generator.

A slim, dependency-light static site in the classic-`site/` style: Python's stdlib `string.Template`
for HTML shells + `markdown` for prose. No web framework. One data file (`content.json`) holds the
site config, nav and section metadata; `content/*.md` holds prose; `templates/*.html` are thin shells.

Showcase hero images are copied at build time from the QA harness output
(`examples/demos/showcase/heroes/`) — the single source of truth (ADR-0037: site media is a release
artifact of the demo QA path). A build self-check fails the build if any section page or hero is
missing, so CI catches a broken site.

    uv run --project site site/build.py --output site/dist
    python3 -m http.server --directory site/dist 8000
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path
from string import Template

import markdown as md

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
TEMPLATES = HERE / "templates"
CONTENT = HERE / "content"
ASSETS = HERE / "assets"
HEROES_SRC = REPO / "examples" / "demos" / "showcase" / "heroes"

MD_EXTENSIONS = ["tables", "fenced_code", "toc", "sane_lists"]


# --- helpers ----------------------------------------------------------------------------------

def load_template(name: str) -> Template:
    return Template((TEMPLATES / name).read_text())


def render_markdown(path: Path) -> str:
    return md.markdown(path.read_text(), extensions=MD_EXTENSIONS)


def ensure_clean_dir(d: Path) -> None:
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)


def nav_links_html(cfg: dict, active_href: str) -> str:
    parts = []
    for item in cfg["nav"]:
        cls = "nav-link active" if item["href"] == active_href else "nav-link"
        rel = ' rel="noopener" target="_blank"' if item.get("external") else ""
        parts.append(f'<a class="{cls}" href="{item["href"]}"{rel}>{item["label"]}</a>')
    return "\n        ".join(parts)


def esc(s: str) -> str:
    return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


# --- section renderers (small item templates keep markup out of long strings) ------------------

def render_supports(cfg: dict) -> str:
    item = Template('<li class="support"><strong>$title</strong><span>$body</span></li>')
    return "\n".join(item.substitute(title=esc(s["title"]), body=esc(s["body"])) for s in cfg["supports"])


def render_showcase_cards(cfg: dict) -> str:
    item = load_template("_showcase_card.html")
    cards = []
    for s in cfg["showcase"]:
        cards.append(item.substitute(
            hero=s["hero"], title=esc(s["title"]), type=s["type"],
            mechanism=esc(s["mechanism"]), blurb=esc(s["blurb"]),
            source=f'{cfg["source_base"]}/{s["source"]}',
        ))
    return "\n".join(cards)


def render_tutorial_cards(cfg: dict) -> str:
    with_hero = load_template("_tutorial_card.html")
    no_hero = load_template("_tutorial_card_nohero.html")
    cards = []
    for t in cfg["tutorials"]:
        tpl = with_hero if t.get("hero") else no_hero
        cards.append(tpl.substitute(
            title=esc(t["title"]), track=esc(t["track"]), difficulty=esc(t["difficulty"]),
            prereqs=esc(t["prereqs"]), build=esc(t["build"]), hero=t.get("hero", ""),
            source=f'{cfg["source_base"]}/examples/tutorials/{t["slug"]}',
        ))
    return "\n".join(cards)


# --- page builder -----------------------------------------------------------------------------

def build_site(output_dir: Path) -> None:
    cfg = json.loads((HERE / "content.json").read_text())
    ensure_clean_dir(output_dir)

    # static assets + hero images (copied from the QA harness output)
    shutil.copytree(ASSETS, output_dir / "assets", dirs_exist_ok=True)
    hero_out = output_dir / "assets" / "showcase"
    hero_out.mkdir(parents=True, exist_ok=True)
    for s in cfg["showcase"]:
        src = HEROES_SRC / s["hero"]
        if not src.exists():
            raise SystemExit(f"[build] missing hero image: {src} (run the showcase harness first)")
        shutil.copy2(src, hero_out / s["hero"])

    base = load_template("base.html")

    def emit(slug: str, title: str, description: str, content_html: str, active: str) -> Path:
        page = base.substitute(
            title=title, meta_description=esc(description),
            nav_links=nav_links_html(cfg, active),
            download_url=cfg["download_url"], github_url=cfg["github_url"],
            content=content_html,
        )
        dest = output_dir if slug == "" else output_dir / slug
        dest.mkdir(parents=True, exist_ok=True)
        (dest / "index.html").write_text(page)
        return dest / "index.html"

    site = cfg["title"]

    # Home
    home = load_template("home.html").substitute(
        tagline=esc(cfg["tagline"]), intro=render_markdown(CONTENT / "home.md"),
        supports=render_supports(cfg), showcase_cards=render_showcase_cards(cfg),
        tutorial_cards=render_tutorial_cards(cfg), download_url=cfg["download_url"],
    )
    emit("", site, cfg["meta_description"], home, "/")

    # Start Here (markdown)
    emit("start-here", f"Start Here — {site}",
         "Download Vivid, install the free beginner instrument, and build your first project.",
         load_template("page.html").substitute(
             title="Start Here", body=render_markdown(CONTENT / "start-here.md")),
         "/start-here/")

    # Tutorials (index of cards)
    emit("tutorials", f"Tutorials — {site}",
         "Release-gated tutorials: each one is a saved, loadable, MCP-inspectable sample project.",
         load_template("tutorials.html").substitute(cards=render_tutorial_cards(cfg)),
         "/tutorials/")

    # Free Plugins (markdown)
    emit("free-plugins", f"Free Plugins — {site}",
         "The curated free-plugin path. Surge XT is the one required beginner instrument.",
         load_template("page.html").substitute(
             title="Free Plugins", body=render_markdown(CONTENT / "free-plugins.md")),
         "/free-plugins/")

    # Showcase (hero gallery)
    emit("showcase", f"Showcase — {site}",
         "Five showcase projects — each a saved, regenerable project, captured from the signed build.",
         load_template("showcase.html").substitute(cards=render_showcase_cards(cfg)),
         "/showcase/")

    # Coming soon
    coming = load_template("coming_soon.html")
    emit("reference", f"Operator Reference — {site}",
         "The operator reference will be generated from Vivid 4 metadata.",
         coming.substitute(
             heading="Operator Reference",
             body="The operator reference is generated from Vivid 4's live operator metadata and "
                  "shader headers. It is coming soon — until then, browse operators in-app over MCP."),
         "/reference/")
    emit("packages", f"Packages — {site}",
         "Community packages are coming soon.",
         coming.substitute(
             heading="Packages",
             body="A community package catalog needs a remote registry and a trust model first. "
                  "Until then, project-local operators and shaders are the creative-coding path — "
                  "see the tutorials."),
         "/packages/")

    _self_check(cfg, output_dir)


def _self_check(cfg: dict, output_dir: Path) -> None:
    expected_pages = ["", "start-here", "tutorials", "free-plugins", "showcase", "reference", "packages"]
    missing = [p or "(home)" for p in expected_pages if not (output_dir / p / "index.html").exists()]
    if missing:
        raise SystemExit(f"[build] self-check failed — missing pages: {', '.join(missing)}")
    missing_heroes = [s["hero"] for s in cfg["showcase"]
                      if not (output_dir / "assets" / "showcase" / s["hero"]).exists()]
    if missing_heroes:
        raise SystemExit(f"[build] self-check failed — missing heroes: {', '.join(missing_heroes)}")
    # No unresolved $template placeholders leaked into any page.
    for html in output_dir.rglob("index.html"):
        text = html.read_text()
        if "$title" in text or "${" in text:
            raise SystemExit(f"[build] self-check failed — unresolved template var in {html}")
    print(f"[build] ok — {len(expected_pages)} pages, {len(cfg['showcase'])} heroes -> {output_dir}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Build the Vivid 4 website")
    ap.add_argument("--output", type=Path, default=HERE / "dist", help="output directory")
    args = ap.parse_args(argv)
    build_site(args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
