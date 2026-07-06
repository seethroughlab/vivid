# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Generate a Sparkle appcast (RSS 2.0 + sparkle namespace) for a Vivid release (P4.5).

The appcast is the feed the in-app updater polls; the release workflow generates it after
building+notarizing the DMG and publishes it. Pure stdlib, so it's unit-smoke-tested
(--selftest) and runnable locally — unlike the signing/notarization steps, which need an
Apple Developer ID.

  uv run scripts/release/generate_appcast.py \\
      --version 0.2.0 --url https://example.com/Vivid-0.2.0.dmg --length 12345678 \\
      --pub-date "Mon, 30 Jun 2026 12:00:00 +0000" --output build/appcast.xml
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from xml.sax.saxutils import escape, quoteattr

SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"


def build_appcast(version: str, url: str, length: int, pub_date: str,
                  title: str = "Vivid", notes_url: str | None = None,
                  ed_signature: str | None = None) -> str:
    """Render an appcast with a single <item>. ed_signature is the Sparkle EdDSA
    signature of the DMG (omitted in dev/unsigned smoke runs)."""
    notes = (f'      <sparkle:releaseNotesLink>{escape(notes_url)}</sparkle:releaseNotesLink>\n'
             if notes_url else "")
    sig_attr = f' sparkle:edSignature={quoteattr(ed_signature)}' if ed_signature else ""
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        f'<rss version="2.0" xmlns:sparkle="{SPARKLE_NS}">\n'
        '  <channel>\n'
        f'    <title>{escape(title)} Updates</title>\n'
        '    <item>\n'
        f'      <title>{escape(title)} {escape(version)}</title>\n'
        f'      <pubDate>{escape(pub_date)}</pubDate>\n'
        f'{notes}'
        f'      <enclosure url={quoteattr(url)} '
        f'sparkle:version={quoteattr(version)} '
        f'sparkle:shortVersionString={quoteattr(version)} '
        f'length="{int(length)}" type="application/octet-stream"{sig_attr} />\n'
        '    </item>\n'
        '  </channel>\n'
        '</rss>\n'
    )


def selftest() -> int:
    import xml.etree.ElementTree as ET
    xml = build_appcast("1.2.3", "https://x/V-1.2.3.dmg", 42, "Mon, 30 Jun 2026 12:00:00 +0000",
                        notes_url="https://x/notes")
    root = ET.fromstring(xml)  # must be well-formed
    enc = root.find(".//enclosure")
    assert enc is not None
    assert enc.get("length") == "42", enc.attrib
    assert enc.get(f"{{{SPARKLE_NS}}}version") == "1.2.3", enc.attrib
    assert enc.get("url") == "https://x/V-1.2.3.dmg"
    # URL-special chars must be safely attribute-escaped.
    xml2 = build_appcast("1.0", 'https://x/a b&c.dmg', 1, "now")
    ET.fromstring(xml2)
    print("generate_appcast selftest: OK")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate a Sparkle appcast for a Vivid release")
    ap.add_argument("--version")
    ap.add_argument("--url")
    ap.add_argument("--length", type=int)
    ap.add_argument("--pub-date", default="")
    ap.add_argument("--title", default="Vivid")
    ap.add_argument("--notes-url")
    ap.add_argument("--ed-signature")
    ap.add_argument("--output", type=Path)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if not (args.version and args.url and args.length is not None):
        print("error: --version, --url, --length are required", file=sys.stderr)
        return 2

    xml = build_appcast(args.version, args.url, args.length, args.pub_date,
                        args.title, args.notes_url, args.ed_signature)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(xml)
        print(f"wrote {args.output}")
    else:
        sys.stdout.write(xml)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
