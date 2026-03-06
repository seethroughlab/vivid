#!/usr/bin/env python3
import argparse
import datetime as dt
import html
from email.utils import format_datetime


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--version", required=True)
    p.add_argument("--url", required=True)
    p.add_argument("--length", required=True)
    p.add_argument("--title", default="Vivid Update")
    p.add_argument("--notes-url", default="")
    p.add_argument("--min-system-version", default="")
    p.add_argument("--signature", default="")
    p.add_argument("--output", required=True)
    args = p.parse_args()

    pub = format_datetime(dt.datetime.now(dt.timezone.utc))
    attrs = [
        f'url="{html.escape(args.url, quote=True)}"',
        f'sparkle:version="{html.escape(args.version, quote=True)}"',
        f'sparkle:shortVersionString="{html.escape(args.version, quote=True)}"',
        f'length="{html.escape(str(args.length), quote=True)}"',
        'type="application/octet-stream"',
    ]
    if args.min_system_version:
        attrs.append(f'sparkle:minimumSystemVersion="{html.escape(args.min_system_version, quote=True)}"')
    if args.signature:
        attrs.append(f'sparkle:edSignature="{html.escape(args.signature, quote=True)}"')

    notes_line = ""
    if args.notes_url:
        notes_line = f"    <sparkle:releaseNotesLink>{html.escape(args.notes_url)}</sparkle:releaseNotesLink>\n"

    xml = f'''<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" xmlns:dc="http://purl.org/dc/elements/1.1/">
  <channel>
    <title>Vivid App Updates</title>
    <link>https://github.com/seethroughlab/vivid/releases</link>
    <description>Stable updates for Vivid</description>
    <language>en</language>
    <item>
      <title>{html.escape(args.title)} {html.escape(args.version)}</title>
      <pubDate>{pub}</pubDate>
{notes_line}      <enclosure {' '.join(attrs)} />
    </item>
  </channel>
</rss>
'''

    with open(args.output, "w", encoding="utf-8") as f:
        f.write(xml)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
