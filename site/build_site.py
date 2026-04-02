#!/usr/bin/env python3
"""Build the Vivid site into a static output directory."""

from __future__ import annotations

import argparse
import html
import json
import re
import shutil
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from string import Template

SCRIPT_DIR = Path(__file__).resolve().parent
TEMPLATES_DIR = SCRIPT_DIR / "src" / "templates"
ASSETS_DIR = SCRIPT_DIR / "src" / "assets"
OPERATORS_DIR = SCRIPT_DIR / "operators"
OPERATOR_INDEX_JSON = OPERATORS_DIR / "index.json"
PACKAGES_JSON = SCRIPT_DIR / "packages.json"
REPOS_JSON = SCRIPT_DIR / "repos.json"
APPCAST_XML = SCRIPT_DIR / "appcast.xml"
DEFAULT_OUTPUT = SCRIPT_DIR / "dist"
GITHUB_SOURCE_BASE = "https://github.com/seethroughlab/vivid/blob/master/"

DOMAIN_ORDER = ["gpu", "audio", "control"]
STATUS_ORDER = {"stable": 0, "beta": 1, "experimental": 2, "deprecated": 3, "broken": 4}
KNOWN_DOMAINS = {"gpu", "audio", "control"}
URL_RE = re.compile(r"^(?:[a-z]+:)?//", re.I)
DOC_BLOCK_RE = re.compile(r"/\*\*(.*?)\*/", re.S)
OPERATOR_NAME_RE = re.compile(r'static\s+constexpr\s+const\s+char\*\s+kName\s*=\s*"([^"]+)"')
PARAM_RE = re.compile(r"vivid::Param<([^>]+)>\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{(.*?)\};", re.S)
PORT_RE = re.compile(r'\{\s*"([^"]+)"\s*,\s*(VIVID_[A-Z0-9_]+)\s*,\s*(VIVID_PORT_(?:INPUT|OUTPUT))')
INLINE_MD_RE = re.compile(r"!\[([^\]]*)\]\(([^)]+)\)|\[([^\]]+)\]\(([^)]+)\)|`([^`]+)`|\*\*([^*]+)\*\*")


@dataclass
class PackageRepo:
    name: str
    repo_url: str
    install_url: str
    default_ref: str
    local_root: Path | None

    @property
    def blob_base(self) -> str:
        owner, repo = parse_repo_url(self.repo_url)
        return f"https://github.com/{owner}/{repo}/blob/{self.default_ref}/"

    @property
    def raw_base(self) -> str:
        owner, repo = parse_repo_url(self.repo_url)
        return f"https://raw.githubusercontent.com/{owner}/{repo}/{self.default_ref}/"

    def read_text(self, rel_path: str) -> str | None:
        if self.local_root is not None:
            path = self.local_root / rel_path
            if path.exists() and path.is_file():
                return path.read_text()
            return None
        try:
            with urllib.request.urlopen(self.raw_base + rel_path, timeout=20) as resp:
                return resp.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                return None
            raise

    def exists(self, rel_path: str) -> bool:
        if self.local_root is not None:
            path = self.local_root / rel_path
            return path.exists() and path.is_file()
        try:
            with urllib.request.urlopen(self.raw_base + rel_path, timeout=20) as resp:
                return resp.status == 200
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                return False
            raise

    def blob_url(self, rel_path: str) -> str:
        return self.blob_base + rel_path

    def raw_url(self, rel_path: str) -> str:
        return self.raw_base + rel_path


@dataclass
class PackageOperatorDoc:
    name: str
    slug: str
    manifest_id: str
    domain: str
    brief: str
    body: str
    source_path: str | None
    source_href: str
    minimal: bool
    params: list[dict]
    ports: list[dict]


@dataclass
class PackageGuide:
    title: str
    slug: str
    path: str
    body_html: str


@dataclass
class PackageDocs:
    package: dict
    slug: str
    has_detail: bool
    overview_html: str
    preview_url: str | None
    operator_pages: list[PackageOperatorDoc]
    guides: list[PackageGuide]


def parse_repo_url(url: str) -> tuple[str, str]:
    match = re.search(r"github\.com[:/]+([^/]+)/([^/.]+)(?:\.git)?$", url)
    if not match:
        raise ValueError(f"Unsupported GitHub URL: {url}")
    return match.group(1), match.group(2)


def load_template(name: str) -> Template:
    return Template((TEMPLATES_DIR / name).read_text())


def ensure_clean_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def esc(value: object) -> str:
    return html.escape("" if value is None else str(value), quote=True)


def slugify(value: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    return slug or "item"


def titleize(value: str) -> str:
    return " ".join(part.capitalize() for part in re.split(r"[_\-]+", value) if part)


def domain_label(domain: str) -> str:
    value = (domain or "").strip().lower()
    if value == "gpu":
        return "GPU"
    if value == "audio":
        return "Audio"
    if value == "control":
        return "Control"
    return value.capitalize() if value else "Operator"


def package_badge_label(category: str) -> str:
    value = (category or "package").strip().replace("_", " ")
    return value.title()


def normalize_type_name(value: str) -> str:
    mapping = {
        "SCALAR": "SCALAR",
        "VIVID_PORT_SCALAR": "SCALAR",
        "VIVID_PORT_SIGNAL": "SCALAR",
        "SIGNAL": "SCALAR",
        "AUDIO_BUFFER": "AUDIO_BUFFER",
        "VIVID_PORT_AUDIO_BUFFER": "AUDIO_BUFFER",
        "VIVID_PORT_AUDIO": "AUDIO_BUFFER",
        "AUDIO": "AUDIO_BUFFER",
        "VIVID_PORT_TEXTURE": "TEXTURE",
        "VIVID_PORT_LANE_ARRAY": "LANE_ARRAY",
        "VIVID_PORT_STRING": "STRING",
        "VIVID_PORT_STRING_LANES": "STRING_LANES",
    }
    return mapping.get(value, value.removeprefix("VIVID_PORT_"))


def resolve_doc_url(url: str, repo: PackageRepo, image: bool = False) -> str:
    if not url:
        return "#"
    if URL_RE.match(url) or url.startswith("mailto:") or url.startswith("#") or url.startswith("/"):
        return url
    base = repo.raw_base if image else repo.blob_base
    return base + url.lstrip("./")


class MarkdownContext:
    def __init__(self, repo: PackageRepo | None = None):
        self.repo = repo

    def resolve(self, url: str, image: bool = False) -> str:
        if self.repo is None:
            return url
        return resolve_doc_url(url, self.repo, image=image)


def inline_md(text: str, md_ctx: MarkdownContext | None = None) -> str:
    md_ctx = md_ctx or MarkdownContext()
    out: list[str] = []
    cursor = 0
    for match in INLINE_MD_RE.finditer(text):
        out.append(html.escape(text[cursor:match.start()]))
        if match.group(1) is not None:
            alt = esc(match.group(1))
            src = esc(md_ctx.resolve(match.group(2), image=True))
            out.append(f'<img class="doc-image" src="{src}" alt="{alt}">')
        elif match.group(3) is not None:
            label = esc(match.group(3))
            href = esc(md_ctx.resolve(match.group(4), image=False))
            out.append(f'<a href="{href}" target="_blank" rel="noopener">{label}</a>')
        elif match.group(5) is not None:
            out.append(f"<code>{esc(match.group(5))}</code>")
        else:
            out.append(f"<strong>{esc(match.group(6))}</strong>")
        cursor = match.end()
    out.append(html.escape(text[cursor:]))
    return "".join(out)


def is_table_separator(line: str) -> bool:
    stripped = line.strip()
    return bool(re.fullmatch(r"\|?\s*[:\-]+(?:\s*\|\s*[:\-]+)+\s*\|?", stripped))


def split_table_row(line: str) -> list[str]:
    stripped = line.strip().strip("|")
    return [cell.strip() for cell in stripped.split("|")]


def markdown_to_html(text: str | None, md_ctx: MarkdownContext | None = None) -> str:
    if not text:
        return ""
    md_ctx = md_ctx or MarkdownContext()
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    html_blocks: list[str] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if not stripped:
            i += 1
            continue
        if stripped.startswith("```"):
            fence = stripped[:3]
            code_lines: list[str] = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith(fence):
                code_lines.append(lines[i])
                i += 1
            if i < len(lines):
                i += 1
            html_blocks.append(f'<pre class="code-block"><code>{esc("\n".join(code_lines))}</code></pre>')
            continue
        if stripped.startswith("#"):
            level = min(len(stripped) - len(stripped.lstrip("#")), 6)
            content = stripped[level:].strip()
            html_blocks.append(f"<h{level}>{inline_md(content, md_ctx)}</h{level}>")
            i += 1
            continue
        if "|" in stripped and i + 1 < len(lines) and is_table_separator(lines[i + 1]):
            headers = split_table_row(lines[i])
            rows: list[list[str]] = []
            i += 2
            while i < len(lines) and "|" in lines[i]:
                rows.append(split_table_row(lines[i]))
                i += 1
            head_html = "".join(f"<th>{inline_md(cell, md_ctx)}</th>" for cell in headers)
            row_html = []
            for row in rows:
                row_html.append("<tr>" + "".join(f"<td>{inline_md(cell, md_ctx)}</td>" for cell in row) + "</tr>")
            html_blocks.append(f'<div class="table-wrap"><table><tr>{head_html}</tr>{"".join(row_html)}</table></div>')
            continue
        if stripped.startswith(("- ", "* ")):
            items: list[str] = []
            while i < len(lines) and lines[i].strip().startswith(("- ", "* ")):
                items.append(lines[i].strip()[2:].strip())
                i += 1
            html_blocks.append("<ul>" + "".join(f"<li>{inline_md(item, md_ctx)}</li>" for item in items) + "</ul>")
            continue
        if re.match(r"\d+\.\s+", stripped):
            items = []
            while i < len(lines) and re.match(r"\d+\.\s+", lines[i].strip()):
                items.append(re.sub(r"^\d+\.\s+", "", lines[i].strip()))
                i += 1
            html_blocks.append("<ol>" + "".join(f"<li>{inline_md(item, md_ctx)}</li>" for item in items) + "</ol>")
            continue
        paragraph: list[str] = []
        while i < len(lines):
            current = lines[i].strip()
            if not current:
                break
            if current.startswith("```") or current.startswith("#") or current.startswith(("- ", "* ")) or re.match(r"\d+\.\s+", current):
                break
            if "|" in current and i + 1 < len(lines) and is_table_separator(lines[i + 1]):
                break
            paragraph.append(current)
            i += 1
        html_blocks.append(f"<p>{inline_md(' '.join(paragraph), md_ctx)}</p>")
    return "\n".join(html_blocks)


def render_table(title: str, headers: list[str], rows: list[list[str]]) -> str:
    if not rows:
        return ""
    head_html = "".join(f"<th>{esc(header)}</th>" for header in headers)
    row_html = []
    for row in rows:
        row_html.append("<tr>" + "".join(f"<td>{cell}</td>" for cell in row) + "</tr>")
    return (
        '<section class="detail-panel">'
        f'<p class="table-label">{esc(title)}</p>'
        '<div class="table-wrap"><table>'
        f'<tr>{head_html}</tr>{"".join(row_html)}'
        '</table></div></section>'
    )


def render_related(related: list[str], by_name: dict[str, dict]) -> str:
    if not related:
        return ""
    chips = []
    for name in related:
        op = by_name.get(name)
        if op:
            chips.append(f'<a href="./../{operator_slug(op)}/">{esc(name)}</a>')
        else:
            chips.append(f"<span>{esc(name)}</span>")
    return (
        '<section class="detail-panel">'
        '<p class="table-label">Related Operators</p>'
        f'<div class="related-links">{"".join(chips)}</div>'
        '</section>'
    )


def operator_slug(operator: dict) -> str:
    return slugify(operator.get("id") or operator.get("name") or "operator")


def render_operator_detail(operator: dict, operator_by_name: dict[str, dict], base_tpl: Template, detail_tpl: Template) -> str:
    source_file = operator.get("source_file")
    source_html = ""
    if source_file:
        href = GITHUB_SOURCE_BASE + source_file
        source_html = (
            '<p class="source-link">Source '
            f'<a href="{esc(href)}" target="_blank" rel="noopener">{esc(source_file)}</a></p>'
        )

    tips_html = ""
    tips = operator.get("tips") or []
    if tips:
        items = "".join(f"<li>{markdown_to_html(tip).replace('<p>', '').replace('</p>', '')}</li>" for tip in tips)
        tips_html = f'<section class="tip-box"><h3>Tips</h3><ul>{items}</ul></section>'

    param_rows = []
    for param in operator.get("params") or []:
        type_or_choices = ", ".join(param.get("choices") or []) or esc(param.get("type", ""))
        range_text = ""
        if param.get("min") is not None and param.get("max") is not None:
            range_text = f"{param['min']} .. {param['max']}"
        default_text = "" if param.get("default") is None else str(param.get("default"))
        param_rows.append([
            esc(param.get("name", "")),
            esc(type_or_choices),
            esc(range_text),
            esc(default_text),
            esc(param.get("doc", "")),
        ])
    params_html = render_table("Parameters", ["Name", "Type", "Range", "Default", "Description"], param_rows)

    input_rows = [[esc(port.get("name", "")), esc(port.get("type", "")), esc(port.get("doc", ""))] for port in operator.get("inputs") or []]
    inputs_html = render_table("Inputs", ["Name", "Type", "Description"], input_rows)

    output_rows = [[esc(port.get("name", "")), esc(port.get("type", "")), esc(port.get("doc", ""))] for port in operator.get("outputs") or []]
    outputs_html = render_table("Outputs", ["Name", "Type", "Description"], output_rows)

    content = detail_tpl.substitute(
        operators_href="../",
        name=esc(operator.get("name", "Operator")),
        domain_class=esc(operator.get("domain", "control")),
        domain_label=esc(domain_label(operator.get("domain", ""))),
        brief=esc(operator.get("brief", "Documentation coming soon.")),
        description_html=markdown_to_html(operator.get("description")) or '<div class="detail-body"><p>No detailed documentation yet.</p></div>',
        source_html=source_html,
        tips_html=tips_html,
        params_html=params_html,
        inputs_html=inputs_html,
        outputs_html=outputs_html,
        related_html=render_related(operator.get("related") or [], operator_by_name),
    )
    return base_tpl.substitute(
        title=f"{operator.get('name', 'Operator')} · Vivid Operator Reference",
        meta_description=esc(operator.get("brief") or operator.get("description") or "Vivid operator reference"),
        asset_prefix="../../",
        content=content,
    )


def render_operator_index(operators: list[dict], base_tpl: Template, index_tpl: Template) -> str:
    sections: list[str] = []
    for domain in DOMAIN_ORDER:
        items = [op for op in operators if op.get("domain") == domain]
        if not items:
            continue
        cards = []
        for op in items:
            cards.append(
                '<article class="op-card">'
                '<div class="op-card-header">'
                f'<h3><a href="./{operator_slug(op)}/">{esc(op.get("name", "Operator"))}</a></h3>'
                f'<span class="badge {esc(domain)}">{esc(domain_label(domain))}</span>'
                '</div>'
                f'<p>{esc(op.get("brief", "Documentation coming soon."))}</p>'
                '</article>'
            )
        sections.append(
            '<section class="section-group">'
            f'<p class="section-kicker">{esc(domain_label(domain))} · {len(items)}</p>'
            f'<div class="grid">{"".join(cards)}</div>'
            '</section>'
        )
    content = index_tpl.substitute(home_href="../", domain_sections="".join(sections))
    return base_tpl.substitute(
        title="Vivid Operator Reference",
        meta_description="Browse all built-in Vivid operators across GPU, audio, and control domains.",
        asset_prefix="../",
        content=content,
    )


def render_home(base_tpl: Template, home_tpl: Template) -> str:
    return base_tpl.substitute(
        title="Vivid",
        meta_description="A real-time creative coding environment for visual and audio performance.",
        asset_prefix="./",
        content=home_tpl.substitute(),
    )


def parse_doc_block(text: str) -> dict | None:
    best: dict | None = None
    for block in DOC_BLOCK_RE.findall(text):
        lines = []
        for raw in block.splitlines():
            cleaned = re.sub(r"^\s*\*\s?", "", raw).rstrip()
            lines.append(cleaned)
        brief = ""
        body_lines: list[str] = []
        param_docs: dict[str, str] = {}
        for line in lines:
            stripped = line.strip()
            if not stripped:
                body_lines.append("")
                continue
            if stripped.startswith("@brief"):
                brief = stripped[len("@brief"):].strip()
                continue
            if stripped.startswith("@param"):
                parts = stripped[len("@param"):].strip().split(None, 1)
                if parts:
                    param_docs[parts[0]] = parts[1].strip() if len(parts) > 1 else ""
                continue
            if stripped.startswith("@"):
                continue
            body_lines.append(stripped)
        body = "\n".join(body_lines).strip()
        if not brief and body:
            paragraphs = [part.strip() for part in body.split("\n\n") if part.strip()]
            if paragraphs:
                brief = paragraphs[0]
                body = "\n\n".join(paragraphs[1:])
        if brief or body or param_docs:
            candidate = {"brief": brief, "body": body, "param_docs": param_docs}
            if candidate["brief"]:
                return candidate
            best = best or candidate
    return best


def extract_kname(text: str) -> str | None:
    match = OPERATOR_NAME_RE.search(text)
    return match.group(1) if match else None


def infer_domain(manifest_id: str, texts: list[str]) -> str:
    if "/" in manifest_id:
        prefix = manifest_id.split("/", 1)[0].lower()
        if prefix in KNOWN_DOMAINS:
            return prefix
    merged = "\n".join(texts)
    if "GpuProcessable" in merged:
        return "gpu"
    if "AudioProcessable" in merged:
        return "audio"
    if "FrameProcessable" in merged:
        return "control"
    return "control"


def extract_param_metadata(texts: list[str], param_docs: dict[str, str]) -> list[dict]:
    rows: list[dict] = []
    seen: set[str] = set()
    for text in texts:
        for match in PARAM_RE.finditer(text):
            type_name, var_name, body = match.groups()
            quoted = re.findall(r'"([^"]+)"', body)
            public_name = quoted[0] if quoted else var_name
            key = f"{var_name}:{public_name}"
            if key in seen:
                continue
            seen.add(key)
            rows.append(
                {
                    "public_name": public_name,
                    "var_name": var_name,
                    "type": type_name.strip(),
                    "doc": param_docs.get(public_name) or param_docs.get(var_name) or "",
                }
            )
    return rows


def extract_port_metadata(texts: list[str]) -> list[dict]:
    rows: list[dict] = []
    seen: set[tuple[str, str, str]] = set()
    for text in texts:
        for match in PORT_RE.finditer(text):
            name, type_name, direction = match.groups()
            key = (name, type_name, direction)
            if key in seen:
                continue
            seen.add(key)
            rows.append(
                {
                    "name": name,
                    "type": normalize_type_name(type_name),
                    "direction": "Input" if direction.endswith("INPUT") else "Output",
                }
            )
    return rows


def source_candidates(manifest_id: str) -> list[str]:
    base = manifest_id.split("/")[-1]
    candidates: list[str] = []
    if "/" in manifest_id:
        candidates.extend(
            [
                f"operators/{manifest_id}/{base}.h",
                f"operators/{manifest_id}/{base}.cpp",
                f"{manifest_id}/{base}.h",
                f"{manifest_id}/{base}.cpp",
                f"src/{base}.h",
                f"src/{base}.cpp",
            ]
        )
    else:
        candidates.extend(
            [
                f"src/{base}.h",
                f"src/{base}.cpp",
                f"src/{base}_op.h",
                f"src/{base}_op.cpp",
                f"operators/audio/{base}/{base}.h",
                f"operators/audio/{base}/{base}.cpp",
                f"operators/control/{base}/{base}.h",
                f"operators/control/{base}/{base}.cpp",
                f"operators/gpu/{base}/{base}.h",
                f"operators/gpu/{base}/{base}.cpp",
            ]
        )
    deduped: list[str] = []
    for candidate in candidates:
        if candidate not in deduped:
            deduped.append(candidate)
    return deduped


def build_package_operator_doc(repo: PackageRepo, package: dict, manifest_id: str) -> PackageOperatorDoc:
    found: list[tuple[str, str]] = []
    for rel_path in source_candidates(manifest_id):
        text = repo.read_text(rel_path)
        if text is not None:
            found.append((rel_path, text))
    texts = [text for _, text in found]
    doc_source_path = found[0][0] if found else None
    doc = None
    display_name = titleize(manifest_id.split("/")[-1])
    for rel_path, text in found:
        display_name = extract_kname(text) or display_name
        parsed = parse_doc_block(text)
        if parsed and doc is None:
            doc = parsed
            doc_source_path = rel_path
    domain = infer_domain(manifest_id, texts)
    brief = doc["brief"] if doc and doc.get("brief") else f"Source-derived documentation for {display_name}."
    body = doc["body"] if doc and doc.get("body") else ""
    param_docs = doc.get("param_docs", {}) if doc else {}
    params = extract_param_metadata(texts, param_docs)
    ports = extract_port_metadata(texts)
    minimal = doc is None
    if minimal:
        print(f"warning: {package['name']}:{manifest_id} has no /** @brief ... */ doc block; rendering minimal operator page", file=sys.stderr)
    source_href = repo.blob_url(doc_source_path) if doc_source_path else repo.repo_url
    return PackageOperatorDoc(
        name=display_name,
        slug=slugify(manifest_id.split("/")[-1]),
        manifest_id=manifest_id,
        domain=domain,
        brief=brief,
        body=body,
        source_path=doc_source_path,
        source_href=source_href,
        minimal=minimal,
        params=params,
        ports=ports,
    )


def build_package_docs(package: dict, local_packages: bool) -> PackageDocs:
    repo = PackageRepo(
        name=package["name"],
        repo_url=package["repo_url"],
        install_url=package["install_url"],
        default_ref=package.get("default_ref") or "master",
        local_root=(SCRIPT_DIR.parent.parent / package["name"]) if local_packages else None,
    )
    site_docs = package.get("site_docs") or {}
    overview_path = site_docs.get("overview") or "README.md"
    readme_text = repo.read_text(overview_path)
    if site_docs.get("overview") and readme_text is None:
        raise SystemExit(f"Declared overview path missing for {package['name']}: {overview_path}")

    guides: list[PackageGuide] = []
    for guide in site_docs.get("guides") or []:
        title = guide.get("title") or Path(guide.get("path", "guide.md")).stem.replace("-", " ").title()
        path = guide.get("path", "")
        body = repo.read_text(path)
        if body is None:
            raise SystemExit(f"Declared guide path missing for {package['name']}: {path}")
        guides.append(
            PackageGuide(
                title=title,
                slug=slugify(title),
                path=path,
                body_html=markdown_to_html(body, MarkdownContext(repo)),
            )
        )

    preview_declared = (site_docs.get("preview_image") or "").strip()
    if preview_declared and not repo.exists(preview_declared):
        raise SystemExit(f"Declared preview image missing for {package['name']}: {preview_declared}")
    preview_url = repo.raw_url(preview_declared) if preview_declared else package.get("preview_image_url")

    operator_entries = []
    operator_entries.extend(package.get("operators") or [])
    operator_entries.extend(package.get("gpu_operators") or [])
    operator_pages: list[PackageOperatorDoc] = []
    if readme_text:
        operator_pages = [build_package_operator_doc(repo, package, manifest_id) for manifest_id in operator_entries]

    return PackageDocs(
        package=package,
        slug=slugify(package["name"]),
        has_detail=bool(readme_text),
        overview_html=markdown_to_html(readme_text, MarkdownContext(repo)) if readme_text else "",
        preview_url=preview_url,
        operator_pages=operator_pages,
        guides=guides,
    )


def render_package_catalog(packages: list[dict], package_docs: dict[str, PackageDocs], base_tpl: Template, catalog_tpl: Template) -> str:
    def sort_key(pkg: dict) -> tuple[int, str]:
        return (STATUS_ORDER.get(str(pkg.get("status", "experimental")).lower(), 99), str(pkg.get("name", "")).lower())

    cards = []
    for pkg in sorted(packages, key=sort_key):
        docs = package_docs[pkg["name"]]
        preview = docs.preview_url or pkg.get("preview_image_url")
        preview_html = f'<img class="thumb" src="{esc(preview)}" alt="{esc(pkg.get("name", "package"))} preview">' if preview else ""
        status = str(pkg.get("status") or "listed").lower()
        status_html = f'<span class="badge {esc(status)}">{esc(status)}</span>'
        status_note = pkg.get("status_note")
        status_note_html = f'<p class="meta">{esc(status_note)}</p>' if status_note else ""
        actions = []
        if docs.has_detail:
            actions.append(f'<a href="../packages/{esc(docs.slug)}/">Package Docs</a>')
        if pkg.get("repo_url"):
            actions.append(f'<a href="{esc(pkg["repo_url"])}" target="_blank" rel="noopener">Repo</a>')
        if pkg.get("homepage_url"):
            actions.append(f'<a href="{esc(pkg["homepage_url"])}" target="_blank" rel="noopener">Homepage</a>')
        docs_note = "Package docs available" if docs.has_detail else "Catalog only"
        title_html = (
            f'<a href="../packages/{esc(docs.slug)}/">{esc(pkg.get("name", "package"))}</a>'
            if docs.has_detail
            else esc(pkg.get("name", "package"))
        )
        cards.append(
            '<article class="card">'
            f'{preview_html}'
            '<div class="card-header">'
            f'<h2>{title_html}</h2>{status_html}'
            '</div>'
            f'<p>{esc(pkg.get("description_short") or pkg.get("description") or "")}</p>'
            f'{status_note_html}'
            f'<p class="meta">v{esc(pkg.get("version", "?"))} · {esc(pkg.get("category", "uncategorized"))} · {esc(docs_note)}</p>'
            f'<div class="actions">{"".join(actions)}</div>'
            f'<code class="install-command">./build/vivid install {esc(pkg.get("install_url") or pkg.get("url") or "")}</code>'
            '</article>'
        )
    content = catalog_tpl.substitute(home_href="../", package_cards="".join(cards))
    return base_tpl.substitute(
        title="Vivid Package Catalog",
        meta_description="Curated package catalog and central package documentation for the Vivid runtime.",
        asset_prefix="../",
        content=content,
    )


def render_package_detail(package_docs: PackageDocs, base_tpl: Template, detail_tpl: Template) -> str:
    package = package_docs.package
    actions = []
    if package.get("repo_url"):
        actions.append(f'<a href="{esc(package["repo_url"])}" target="_blank" rel="noopener">Repo</a>')
    if package.get("homepage_url"):
        actions.append(f'<a href="{esc(package["homepage_url"])}" target="_blank" rel="noopener">Homepage</a>')
    tags = package.get("tags") or []
    tags_html = ""
    if tags:
        tags_html = '<div class="related-links">' + "".join(f'<span>{esc(tag)}</span>' for tag in tags) + '</div>'
    preview_html = ""
    if package_docs.preview_url:
        preview_html = f'<img class="package-preview" src="{esc(package_docs.preview_url)}" alt="{esc(package["name"])} preview">'
    guides_html = ""
    if package_docs.guides:
        cards = []
        for guide in package_docs.guides:
            cards.append(
                '<article class="op-card">'
                f'<h3><a href="./guides/{esc(guide.slug)}/">{esc(guide.title)}</a></h3>'
                '<p>Guide included in this package documentation set.</p>'
                '</article>'
            )
        guides_html = '<section class="detail-panel"><p class="table-label">Guides</p><div class="grid">' + "".join(cards) + '</div></section>'
    operators_html = ""
    if package_docs.operator_pages:
        cards = []
        for op in package_docs.operator_pages:
            note = "Source docs" if not op.minimal else "Minimal page"
            cards.append(
                '<article class="op-card">'
                '<div class="op-card-header">'
                f'<h3><a href="./operators/{esc(op.slug)}/">{esc(op.name)}</a></h3>'
                f'<span class="badge {esc(op.domain)}">{esc(domain_label(op.domain))}</span>'
                '</div>'
                f'<p>{esc(op.brief)}</p>'
                f'<p class="meta">{esc(op.manifest_id)} · {esc(note)}</p>'
                '</article>'
            )
        operators_html = '<section class="detail-panel"><p class="table-label">Operators</p><div class="grid">' + "".join(cards) + '</div></section>'
    content = detail_tpl.substitute(
        catalog_href="../../catalog/",
        name=esc(package["name"]),
        status_class=esc(str(package.get("status") or "listed").lower()),
        status_label=esc(str(package.get("status") or "listed")),
        category_class="package",
        category_label=esc(package_badge_label(package.get("category", "package"))),
        description=esc(package.get("description_short") or package.get("description") or ""),
        version=esc(package.get("version", "?")),
        vivid_core=esc(package.get("vivid_core") or "unspecified"),
        author=esc(package.get("author") or "unknown"),
        tags_html=tags_html,
        preview_html=preview_html,
        actions_html="".join(actions),
        install_url=esc(package.get("install_url") or package.get("url") or ""),
        overview_html=package_docs.overview_html,
        guides_html=guides_html,
        operators_html=operators_html,
    )
    return base_tpl.substitute(
        title=f"{package['name']} · Vivid Package Docs",
        meta_description=esc(package.get("description") or package.get("description_short") or package["name"]),
        asset_prefix="../../",
        content=content,
    )


def render_package_guide(package_docs: PackageDocs, guide: PackageGuide, base_tpl: Template, guide_tpl: Template) -> str:
    package = package_docs.package
    content = guide_tpl.substitute(
        package_href="../../",
        package_name=esc(package["name"]),
        title=esc(guide.title),
        category_class="package",
        category_label=esc(package_badge_label(package.get("category", "package"))),
        body_html=guide.body_html,
    )
    return base_tpl.substitute(
        title=f"{guide.title} · {package['name']} · Vivid",
        meta_description=esc(f"{guide.title} guide for {package['name']}"),
        asset_prefix="../../../../",
        content=content,
    )


def render_package_operator(package_docs: PackageDocs, op: PackageOperatorDoc, base_tpl: Template, operator_tpl: Template) -> str:
    package = package_docs.package
    description_html = '<div class="detail-body"><p>No source doc block yet. This page was generated from the package manifest and source metadata.</p></div>'
    if op.body:
        description_html = f'<div class="detail-body">{markdown_to_html(op.body)}</div>'
    minimal_note = ''
    if op.minimal:
        minimal_note = '<p class="footer-note">Minimal page: add a <code>/** @brief ... */</code> doc block in the operator source for richer published docs.</p>'
    param_rows = [[esc(item["public_name"]), esc(item["type"]), esc(item["doc"] or "—")] for item in op.params]
    params_html = render_table("Parameters", ["Name", "Type", "Description"], param_rows)
    port_rows = [[esc(item["name"]), esc(item["direction"]), esc(item["type"])] for item in op.ports]
    ports_html = render_table("Ports", ["Name", "Direction", "Type"], port_rows)
    content = operator_tpl.substitute(
        package_href="../../",
        package_name=esc(package["name"]),
        name=esc(op.name),
        domain_class=esc(op.domain),
        domain_label=esc(domain_label(op.domain)),
        category_class="package",
        category_label=esc(package_badge_label(package.get("category", "package"))),
        brief=esc(op.brief),
        description_html=description_html,
        source_href=esc(op.source_href),
        source_path=esc(op.source_path or op.manifest_id),
        minimal_note=minimal_note,
        params_html=params_html,
        ports_html=ports_html,
    )
    return base_tpl.substitute(
        title=f"{op.name} · {package['name']} · Vivid",
        meta_description=esc(op.brief),
        asset_prefix="../../../../",
        content=content,
    )


def build_site(output_dir: Path, local_packages: bool) -> None:
    ensure_clean_dir(output_dir)
    shutil.copytree(ASSETS_DIR, output_dir / "assets", dirs_exist_ok=True)
    shutil.copy2(PACKAGES_JSON, output_dir / "packages.json")
    shutil.copy2(APPCAST_XML, output_dir / "appcast.xml")

    base_tpl = load_template("base.html")
    home_tpl = load_template("home.html")
    operator_index_tpl = load_template("operator_index.html")
    operator_detail_tpl = load_template("operator_detail.html")
    catalog_tpl = load_template("catalog_index.html")
    package_detail_tpl = load_template("package_detail.html")
    package_guide_tpl = load_template("package_guide.html")
    package_operator_tpl = load_template("package_operator_detail.html")

    operator_index = json.loads(OPERATOR_INDEX_JSON.read_text())
    operators = sorted(operator_index.get("operators", []), key=lambda op: op.get("name", "").lower())
    operator_by_name = {op.get("name", ""): op for op in operators}

    for op in operators:
        detail_path = OPERATORS_DIR / f"{op['id']}.json"
        if detail_path.exists():
            detail = json.loads(detail_path.read_text())
            merged = dict(op)
            merged.update(detail)
        else:
            merged = dict(op)
        page = render_operator_detail(merged, operator_by_name, base_tpl, operator_detail_tpl)
        write_text(output_dir / "operators" / operator_slug(merged) / "index.html", page)

    operator_index_html = render_operator_index(operators, base_tpl, operator_index_tpl)
    write_text(output_dir / "operators" / "index.html", operator_index_html)

    packages = json.loads(PACKAGES_JSON.read_text()).get("packages", [])
    package_docs = {pkg["name"]: build_package_docs(pkg, local_packages=local_packages) for pkg in packages}
    catalog_html = render_package_catalog(packages, package_docs, base_tpl, catalog_tpl)
    write_text(output_dir / "catalog" / "index.html", catalog_html)

    for pkg in packages:
        docs = package_docs[pkg["name"]]
        if not docs.has_detail:
            continue
        write_text(output_dir / "packages" / docs.slug / "index.html", render_package_detail(docs, base_tpl, package_detail_tpl))
        for guide in docs.guides:
            write_text(output_dir / "packages" / docs.slug / "guides" / guide.slug / "index.html", render_package_guide(docs, guide, base_tpl, package_guide_tpl))
        for op in docs.operator_pages:
            write_text(output_dir / "packages" / docs.slug / "operators" / op.slug / "index.html", render_package_operator(docs, op, base_tpl, package_operator_tpl))

    home_html = render_home(base_tpl, home_tpl)
    write_text(output_dir / "index.html", home_html)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT), help="Output directory for generated HTML")
    parser.add_argument(
        "--local-packages",
        action="store_true",
        help="Read package docs from sibling repos instead of fetching GitHub raw files",
    )
    args = parser.parse_args()
    build_site(Path(args.output).resolve(), local_packages=args.local_packages)


if __name__ == "__main__":
    main()
