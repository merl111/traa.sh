#!/usr/bin/env python3
"""Validate internal HTML, asset, and fragment links under docs/.

No third-party dependencies. Exit 0 on success, 1 if any link is broken.
"""

from __future__ import annotations

import re
import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlparse

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"

ATTR_HREF_SRC = re.compile(
    r"""(?:href|src)\s*=\s*(?P<q>['"])(?P<url>.*?)(?P=q)""",
    re.IGNORECASE,
)
ID_RE = re.compile(
    r"""\b(?:id|name)\s*=\s*(?P<q>['"])(?P<id>[^'"]+)(?P=q)""",
    re.IGNORECASE,
)


class LinkCollector(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.links: list[tuple[int, str]] = []
        self.ids: set[str] = set()

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        line = self.getpos()[0]
        amap = {k.lower(): v for k, v in attrs if v is not None}
        if "id" in amap:
            self.ids.add(amap["id"])
        if tag.lower() == "a" and "name" in amap:
            self.ids.add(amap["name"])
        for key in ("href", "src"):
            if key in amap:
                self.links.append((line, amap[key]))


def collect_ids(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8")
    out: set[str] = set()
    for match in ID_RE.finditer(text):
        out.add(match.group("id"))
    parser = LinkCollector()
    try:
        parser.feed(text)
        out |= parser.ids
    except Exception:
        pass
    return out


def is_external(url: str) -> bool:
    parsed = urlparse(url)
    if parsed.scheme in {"http", "https", "mailto", "javascript", "data"}:
        return True
    if url.startswith("//"):
        return True
    return False


def check() -> int:
    if not DOCS.is_dir():
        print(f"error: missing docs directory: {DOCS}", file=sys.stderr)
        return 1

    html_files = sorted(p for p in DOCS.rglob("*.html") if "node_modules" not in p.parts)
    errors: list[str] = []
    expected = [
        DOCS / "index.html",
        DOCS / "agents.html",
        DOCS / "getting-started.html",
        DOCS / "keyboard-ui.html",
        DOCS / "plugins.html",
        DOCS / "llms.txt",
        DOCS / "assets" / "site.css",
        DOCS / "assets" / "traash.svg",
    ]
    for path in expected:
        if not path.exists():
            errors.append(f"missing required file: {path.relative_to(ROOT)}")

    id_cache: dict[Path, set[str]] = {}

    def ids_for(path: Path) -> set[str]:
        if path not in id_cache:
            id_cache[path] = collect_ids(path) if path.suffix.lower() in {".html", ".htm"} else set()
        return id_cache[path]

    for html in html_files:
        text = html.read_text(encoding="utf-8")
        parser = LinkCollector()
        try:
            parser.feed(text)
            links = parser.links
        except Exception:
            links = [(0, m.group("url")) for m in ATTR_HREF_SRC.finditer(text)]

        for line, raw in links:
            url = raw.strip()
            if not url or is_external(url):
                continue
            parsed = urlparse(url)
            fragment = unquote(parsed.fragment) if parsed.fragment else ""
            rel = unquote(parsed.path)
            if not rel:
                target = html
            else:
                target = (html.parent / rel).resolve()
            try:
                target.relative_to(DOCS.resolve())
            except ValueError:
                errors.append(
                    f"{html.relative_to(ROOT)}:{line}: link escapes docs/: {url}"
                )
                continue
            if not target.exists():
                loc = f"{html.relative_to(ROOT)}:{line}" if line else str(html.relative_to(ROOT))
                errors.append(f"{loc}: missing {url} -> {target.relative_to(ROOT)}")
                continue
            if fragment:
                if target.suffix.lower() not in {".html", ".htm"}:
                    continue
                if fragment not in ids_for(target):
                    loc = f"{html.relative_to(ROOT)}:{line}" if line else str(html.relative_to(ROOT))
                    errors.append(f"{loc}: missing anchor #{fragment} in {target.relative_to(ROOT)}")

    if errors:
        print(f"docs-check: {len(errors)} problem(s)")
        for err in errors:
            print(f"  {err}")
        return 1
    print(f"docs-check: ok ({len(html_files)} html files, {len(expected)} required paths)")
    return 0


if __name__ == "__main__":
    sys.exit(check())
