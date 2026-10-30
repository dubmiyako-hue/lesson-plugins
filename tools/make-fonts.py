#!/usr/bin/env python
"""Make plugin/web/fonts/: the PRAYROOM faces cut down to the characters the screen uses.

The web app loads the whole families from @fontsource (122 slices per weight, about 2.4 MB a weight), because its text
is Japanese. The plug-in carries its page in the binary, so it keeps only the glyphs of the characters that occur in
plugin/web (the copied screen, the pages, PRAYROOM's bundle.js) and printable ASCII: each @fontsource slice is cut to
those characters and the slices of a weight are merged into one font. The result is WOFF 1 (zlib), since fontTools needs
the Brotli module to read or write WOFF 2 and the .woff files of the same slices are in the packages too.

    python tools/make-fonts.py

Needs fontTools and the npm packages (npm install). Writes the five fonts, chars.txt (the characters the Zen Kaku
Gothic New fonts hold, which a test checks the screen's text against) and the two licences. Run it again whenever the
screen's text gets a character it did not have; the fonts are in the repository, so a build does not need fontTools.
"""
import io
import pathlib
import re
import sys

from fontTools import subset
from fontTools.merge import Merger, Options as MergeOptions
from fontTools.ttLib import TTFont

REPO = pathlib.Path(__file__).resolve().parent.parent
WEB = REPO / "plugin" / "web"
OUT = WEB / "fonts"
MODULES = REPO / "node_modules" / "@fontsource"

# (package, family, weights, file name stem)
FAMILIES = [
    ("zen-kaku-gothic-new", "Zen Kaku Gothic New", [400, 500, 700], "ZenKakuGothicNew"),
    ("ibm-plex-mono", "IBM Plex Mono", [400, 500], "IBMPlexMono"),
]
TEXT_SUFFIXES = {".ts", ".tsx", ".css", ".html", ".js"}


def screen_text() -> set[int]:
    """Every character of plugin/web's sources (dist and fonts aside), with printable ASCII."""
    points = set(range(0x20, 0x7F))
    for p in WEB.rglob("*"):
        if not p.is_file() or p.suffix not in TEXT_SUFFIXES:
            continue
        rel = p.relative_to(WEB).parts
        if rel[0] in ("dist", "fonts", "node_modules"):
            continue
        points |= {ord(c) for c in p.read_text(encoding="utf-8") if ord(c) > 0x7E}
    return points


def slices(package: str, weight: int) -> list[tuple[pathlib.Path, set[int]]]:
    """The normal-style slices of a weight, each with the code points its @font-face rule's unicode-range holds."""
    css = (MODULES / package / f"{weight}.css").read_text(encoding="utf-8")
    found = []
    for m in re.finditer(r"url\(\./files/([^)]+?)\.woff2\).*?unicode-range:\s*([^;]+);", css, re.S):
        name, ranges = m.group(1), m.group(2)
        if "italic" in name:
            continue
        points = set()
        for part in ranges.split(","):
            a, _, b = part.strip()[2:].partition("-")
            points |= set(range(int(a, 16), int(b or a, 16) + 1))
        found.append((MODULES / package / "files" / f"{name}.woff", points))
    if not found:
        raise SystemExit(f"make-fonts: no slices in {package}/{weight}.css")
    return found


def make(package: str, weight: int, stem: str, wanted: set[int], work: pathlib.Path) -> set[int]:
    parts = []
    for path, points in slices(package, weight):
        need = wanted & points
        if not need:
            continue
        need &= set(TTFont(path).getBestCmap())
        if not need:
            continue
        options = subset.Options()
        options.layout_features = ["*"]
        options.notdef_outline = True
        font = TTFont(path)
        cutter = subset.Subsetter(options)
        cutter.populate(unicodes=need)
        cutter.subset(font)
        font.flavor = None
        part = work / f"{path.stem}.ttf"
        font.save(part)
        parts.append(part)
    # The merger does not take the vertical metrics tables, which the page does not use (no vertical writing).
    merged = Merger(options=MergeOptions(drop_tables=["vmtx", "vhea", "VORG"])).merge([str(p) for p in parts])
    merged.flavor = "woff"
    target = OUT / f"{stem}-{weight}.woff"
    merged.save(target)
    held = set(TTFont(target).getBestCmap())
    print(f"{target.name}: {len(parts)} slices, {target.stat().st_size} bytes, {len(held)} characters")
    return held


def main() -> int:
    import tempfile

    wanted = screen_text()
    OUT.mkdir(parents=True, exist_ok=True)
    zen = None
    with tempfile.TemporaryDirectory() as work:
        for package, family, weights, stem in FAMILIES:
            for weight in weights:
                held = make(package, weight, stem, wanted, pathlib.Path(work))
                if package == "zen-kaku-gothic-new":
                    zen = held if zen is None else zen & held
            licence = (MODULES / package / "LICENSE").read_bytes()
            (OUT / f"OFL-{stem}.txt").write_bytes(licence)
    missing = sorted(c for c in wanted if c not in zen)
    if missing:
        print("make-fonts: Zen Kaku Gothic New has no glyph for " + "".join(chr(c) for c in missing), file=sys.stderr)
    # One character per line, by code point; the test reads it back.
    (OUT / "chars.txt").write_text("".join(chr(c) + "\n" for c in sorted(zen) if c > 0x20), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
