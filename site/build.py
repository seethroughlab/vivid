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


def showcase_media_html(s: dict) -> str:
    """The card's media element: a muted autoplay-loop <video> (hero PNG as poster/fallback) when the
    showcase has a clip, else the still <img>. Poster means the hero shows instantly and remains the
    fallback if the video can't play, so the hero PNGs stay the single source of truth."""
    hero = s["hero"]
    title = esc(s["title"])
    if s.get("video"):
        return (
            f'<video class="showcase-video" src="/assets/showcase/{s["video"]}" '
            f'poster="/assets/showcase/{hero}" width="1280" height="720" '
            f'muted loop playsinline autoplay preload="metadata" aria-label="{title}"></video>'
        )
    return (f'<img src="/assets/showcase/{hero}" alt="{title}" '
            f'width="1280" height="720" loading="lazy">')


def render_showcase_cards(cfg: dict) -> str:
    item = load_template("_showcase_card.html")
    cards = []
    for s in cfg["showcase"]:
        cards.append(item.substitute(
            media=showcase_media_html(s), title=esc(s["title"]), type=s["type"],
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


# --- operator reference (ADR-0038: rendered from the generated reference.json snapshot) -------

import re

DOMAIN_ORDER = ["visual", "audio", "control"]
DOMAIN_LABEL = {"visual": "Visual", "audio": "Audio", "control": "Control"}


def slugify(v: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", v.lower()).strip("-")


def _num(v) -> str:
    if isinstance(v, bool):
        return str(v).lower()
    if isinstance(v, (int, float)):
        r = round(float(v), 4)
        return str(int(r)) if r == int(r) else f"{r:g}"
    return esc(str(v))


def _detail_table(label: str, headers: list[str], rows: list[list[str]]) -> str:
    if not rows:
        return ""
    head = "".join(f"<th>{esc(h)}</th>" for h in headers)
    body = "".join("<tr>" + "".join(f"<td>{c}</td>" for c in r) + "</tr>" for r in rows)
    return (f'      <section class="detail-panel">\n'
            f'        <p class="table-label">{esc(label)}</p>\n'
            f'        <div class="table-wrap"><table><thead><tr>{head}</tr></thead>'
            f'<tbody>{body}</tbody></table></div>\n      </section>')


def _param_rows(params: list[dict]) -> list[list[str]]:
    rows = []
    for p in params:
        choices = p.get("choices")
        typ = "choice" if choices else esc(str(p.get("type", "")))
        lo, hi = p.get("min"), p.get("max")
        rng = f"{_num(lo)} – {_num(hi)}" if lo is not None and hi is not None else ""
        default = _num(p["default"]) if "default" in p else ""
        desc = p.get("description") or p.get("semantic_intent") or ""
        if choices:
            desc = (desc + " " if desc else "") + "(" + ", ".join(map(str, choices)) + ")"
        rows.append([f"<code>{esc(str(p.get('name', '')))}</code>", typ, rng, default, esc(desc)])
    return rows


def _port_rows(ports: list[dict]) -> list[list[str]]:
    return [[f"<code>{esc(str(p.get('name', '')))}</code>", esc(p.get("dir", "")),
             esc(p.get("description") or p.get("semantic_intent") or "")] for p in ports]


def render_reference_cards(ops: list[dict]) -> str:
    sections = []
    for dom in DOMAIN_ORDER:
        items = [o for o in ops if o["domain"] == dom]
        if not items:
            continue
        cards = []
        for o in items:
            search = " ".join([o["name"], o.get("summary", ""), dom, *o.get("keywords", [])]).lower()
            cards.append(
                f'        <article class="op-card operator-card" data-domain="{dom}" '
                f'data-search="{esc(search)}">\n'
                f'          <div class="op-card-header"><h3><a href="/reference/{o["slug"]}/">'
                f'{esc(o["name"])}</a></h3><span class="badge {dom}">{DOMAIN_LABEL[dom]}</span></div>\n'
                f'          <p>{esc(o.get("summary") or "No description yet.")}</p>\n'
                f'        </article>')
        sections.append(
            f'      <section class="operator-section" data-domain="{dom}">\n'
            f'        <p class="section-kicker">{DOMAIN_LABEL[dom]} · {len(items)}</p>\n'
            f'        <div class="op-grid">\n' + "\n".join(cards) + "\n        </div>\n      </section>")
    return "\n".join(sections)


def render_reference_detail_content(op: dict) -> dict:
    kw = op.get("keywords", [])
    keywords_html = ('        <div class="keywords">'
                     + "".join(f'<span class="keyword">{esc(k)}</span>' for k in kw)
                     + "</div>") if kw else ""
    params_html = _detail_table("Parameters", ["Name", "Type", "Range", "Default", "Description"],
                                _param_rows(op.get("params", [])))
    ports_html = _detail_table("Ports", ["Port", "Direction", "Description"],
                               _port_rows(op.get("ports", [])))
    src = op.get("source") or {}
    source_html = ""
    if src.get("path"):
        import os as _os
        source_html = (f'      <section class="detail-panel"><p class="source-note">Backed by a '
                       f'{esc(src.get("tier", "bundled"))} shader file: '
                       f'<code>{esc(_os.path.basename(src["path"]))}</code></p></section>')
    return {"name": esc(op["name"]), "domain": op["domain"], "domain_label": DOMAIN_LABEL[op["domain"]],
            "kind": esc(op.get("kind", "")), "summary": esc(op.get("summary") or "No description yet."),
            "keywords_html": keywords_html, "params_html": params_html, "ports_html": ports_html,
            "source_html": source_html}


def render_reference(cfg: dict, emit, site: str) -> None:
    """Render /reference/ (filterable index) + /reference/<slug>/ detail pages from reference.json.
    Falls back to a coming-soon page if the snapshot is absent (so the build never breaks)."""
    ref_path = HERE / "reference.json"
    if not ref_path.exists():
        emit("reference", f"Operator Reference — {site}", "The operator reference is coming soon.",
             load_template("coming_soon.html").substitute(
                 heading="Operator Reference",
                 body="Generated from Vivid 4's live operator metadata. Run "
                      "<code>site/generate_reference.py</code> to produce the snapshot."),
             "/reference/")
        return
    ref = json.loads(ref_path.read_text())
    ops = ref.get("operators", [])
    opts = ['            <option value="">All domains</option>']
    for d in ref.get("domains", DOMAIN_ORDER):
        opts.append(f'            <option value="{d}">{DOMAIN_LABEL.get(d, d)}</option>')
    index_html = load_template("reference_index.html").substitute(
        count=ref.get("count", len(ops)), app_version=esc(str(ref.get("app_version", ""))),
        operator_abi=esc(str(ref.get("operator_abi", ""))),
        domain_options="\n".join(opts), domain_sections=render_reference_cards(ops))
    emit("reference", f"Operator Reference — {site}",
         f"{ref.get('count', len(ops))} Vivid operators, generated from live metadata.",
         index_html, "/reference/")
    detail_tpl = load_template("reference_detail.html")
    for o in ops:
        emit(f"reference/{o['slug']}", f"{o['name']} — Operator Reference — {site}",
             (o.get("summary") or f"The {o['name']} operator."),
             detail_tpl.substitute(render_reference_detail_content(o)), "/reference/")


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
        # Optional per-showcase clip (heroes/<id>.mp4 from `runner.py --video`); the hero PNG is its poster.
        if s.get("video"):
            vsrc = HEROES_SRC / s["video"]
            if not vsrc.exists():
                raise SystemExit(f"[build] missing showcase video: {vsrc} "
                                 f"(run the showcase harness with --video, or drop the 'video' field)")
            shutil.copy2(vsrc, hero_out / s["video"])

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

    # Operator Reference (generated from reference.json; ADR-0038)
    render_reference(cfg, emit, site)

    # Coming soon
    coming = load_template("coming_soon.html")
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
    missing_videos = [s["video"] for s in cfg["showcase"]
                      if s.get("video") and not (output_dir / "assets" / "showcase" / s["video"]).exists()]
    if missing_videos:
        raise SystemExit(f"[build] self-check failed — missing videos: {', '.join(missing_videos)}")
    # No unresolved $template placeholders leaked into any page.
    for html in output_dir.rglob("index.html"):
        text = html.read_text()
        if "$title" in text or "${" in text:
            raise SystemExit(f"[build] self-check failed — unresolved template var in {html}")
    # Operator reference: every operator in the snapshot must have a detail page.
    ref_path = HERE / "reference.json"
    ref_ops = 0
    if ref_path.exists():
        ops = json.loads(ref_path.read_text()).get("operators", [])
        ref_ops = len(ops)
        missing = [o["slug"] for o in ops if not (output_dir / "reference" / o["slug"] / "index.html").exists()]
        if missing:
            raise SystemExit(f"[build] self-check failed — missing operator pages: {', '.join(missing[:5])}")
    print(f"[build] ok — {len(expected_pages)} pages, {len(cfg['showcase'])} heroes, "
          f"{ref_ops} operator pages -> {output_dir}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Build the Vivid 4 website")
    ap.add_argument("--output", type=Path, default=HERE / "dist", help="output directory")
    args = ap.parse_args(argv)
    build_site(args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
