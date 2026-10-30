#!/usr/bin/env python
"""Copy PRAYROOM's tokens.css, bundle.css, bundle.js and index.d.ts into plugin/web/prayroom/.

The source is the copy the web app uses (mix-course/prayroom), so the screen is drawn by the same components as the web
app's. The files are copied byte for byte, except that the @import of Google Fonts at the top of bundle.css is replaced
by @font-face rules for the fonts in plugin/web/fonts/ (tools/make-fonts.py), so the page needs no network.
SOURCE.txt records where the copy came from and the files' SHA-256. Running it again changes nothing.

    python tools/sync-prayroom.py [--src DIR]
"""
import argparse
import hashlib
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
DST = REPO / "plugin" / "web" / "prayroom"

# (family, weight, file) of plugin/web/fonts/
FONTS = [
    ("Zen Kaku Gothic New", 400, "ZenKakuGothicNew-400.woff"),
    ("Zen Kaku Gothic New", 500, "ZenKakuGothicNew-500.woff"),
    ("Zen Kaku Gothic New", 700, "ZenKakuGothicNew-700.woff"),
    ("IBM Plex Mono", 400, "IBMPlexMono-400.woff"),
    ("IBM Plex Mono", 500, "IBMPlexMono-500.woff"),
]
IMPORT = re.compile(rb'@import url\("https://fonts\.googleapis\.com/[^"]*"\);')


def font_faces(eol: bytes) -> bytes:
    rules = [
        f'@font-face {{ font-family: "{family}"; font-style: normal; font-weight: {weight}; font-display: block; '
        f'src: url("../fonts/{name}") format("woff"); }}'
        for family, weight, name in FONTS
    ]
    return eol.join(r.encode("ascii") for r in rules)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default="D:/work/Claude/mix-course/prayroom", help="PRAYROOM folder (default: %(default)s)")
    a = ap.parse_args()
    src = pathlib.Path(a.src).resolve()
    parts = {"tokens.css": src / "tokens.css", "bundle.css": src / "components" / "bundle.css",
             "bundle.js": src / "components" / "bundle.js", "index.d.ts": src / "components" / "index.d.ts"}
    for p in parts.values():
        if not p.is_file():
            print(f"sync-prayroom: missing {p}", file=sys.stderr)
            return 1
    out = {}
    for name, p in parts.items():
        data = p.read_bytes()
        if name == "bundle.css":
            m = IMPORT.search(data)
            if not m:
                print("sync-prayroom: the @import of Google Fonts was not found in bundle.css", file=sys.stderr)
                return 1
            eol = b"\r\n" if data[m.end():m.end() + 2] == b"\r\n" else b"\n"
            data = data[:m.start()] + font_faces(eol) + data[m.end():]
            if b"fonts.googleapis.com/css" in data:
                print("sync-prayroom: bundle.css loads Google Fonts in a second place", file=sys.stderr)
                return 1
        out[name] = data
    DST.mkdir(parents=True, exist_ok=True)
    lines = [f"Copied by tools/sync-prayroom.py from {src.as_posix()} (the web app's copy of PRAYROOM).",
             "bundle.css has its Google Fonts @import replaced by @font-face rules for ../fonts/.", ""]
    for name, data in out.items():
        f = DST / name
        if not f.is_file() or f.read_bytes() != data:
            f.write_bytes(data)
        lines.append(f"{hashlib.sha256(parts[name].read_bytes()).hexdigest()}  {name} (source)")
    (DST / "SOURCE.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
