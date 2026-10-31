"""Build the distribution zip LessonPlugins-<version>.zip and its .sha256 file.

Usage: python tools/package.py [--out <folder>]
The default output folder is dist/ under the repository root. Build first with tools\\build.cmd Release.
Uses only the standard library. Exits with 1 and lists the files when any input is missing.
"""

import argparse
import hashlib
import os
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
JUCE_DIR = Path("D:/work/Claude/_sdk/JUCE/JUCE-subbass")

# (bundle folder, binary inside it). The whole bundle folder goes into the zip.
BUNDLES = [
    (ROOT / "build-rel/plugin/LessonEQ_artefacts/Release/VST3/Lesson EQ.vst3",
     "Contents/x86_64-win/Lesson EQ.vst3"),
    (ROOT / "build-rel/plugin/LessonComp_artefacts/Release/VST3/Lesson Comp.vst3",
     "Contents/x86_64-win/Lesson Comp.vst3"),
]
RECEIVER_FILES = [ROOT / "tools/dist/install.cmd", ROOT / "tools/dist/README.txt"]
# (source, name inside licenses/)
LICENSES = [
    (ROOT / "LICENSE", "AGPL-3.0-LICENSE.md"),
    (JUCE_DIR / "LICENSE.md", "JUCE-LICENSE.md"),
    (JUCE_DIR / "modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt", "VST3-SDK-LICENSE.txt"),
    (ROOT / "third_party/fdlibm/LICENSE", "fdlibm-LICENSE.txt"),
    (ROOT / "plugin/web/fonts/OFL-IBMPlexMono.txt", "OFL-IBMPlexMono.txt"),
    (ROOT / "plugin/web/fonts/OFL-ZenKakuGothicNew.txt", "OFL-ZenKakuGothicNew.txt"),
    (ROOT / "node_modules/preact/LICENSE", "preact-LICENSE.txt"),
]


def read_version():
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    m = re.search(r"project\(\s*lesson\s+VERSION\s+(\d+\.\d+\.\d+)", text)
    if m is None:
        raise SystemExit("version not found: CMakeLists.txt has no 'project(lesson VERSION x.y.z'")
    return m.group(1)


def missing_inputs():
    missing = []
    for folder, binary in BUNDLES:
        if not (folder / binary).is_file():
            missing.append(folder / binary)
    for path in RECEIVER_FILES:
        if not path.is_file():
            missing.append(path)
    for src, _ in LICENSES:
        if not src.is_file():
            missing.append(src)
    return missing


def to_crlf(data):
    return data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")


def collect_entries(version):
    """Return (name in zip, source) pairs. A source is a Path, or bytes for converted content."""
    top = f"LessonPlugins-{version}"
    entries = []
    for folder, _ in BUNDLES:
        for path in sorted(folder.rglob("*")):
            if path.is_file():
                rel = path.relative_to(folder).as_posix()
                entries.append((f"{top}/{folder.name}/{rel}", path))
    entries.append((f"{top}/install.cmd", to_crlf((ROOT / "tools/dist/install.cmd").read_bytes())))
    entries.append((f"{top}/README.txt", ROOT / "tools/dist/README.txt"))
    for src, name in LICENSES:
        entries.append((f"{top}/licenses/{name}", src))
    return entries


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description="Build the distribution zip.")
    parser.add_argument("--out", type=Path, default=ROOT / "dist",
                        help="output folder (default: dist/ under the repository root)")
    args = parser.parse_args()

    missing = missing_inputs()
    if missing:
        for path in missing:
            print(f"missing: {path}", file=sys.stderr)
        print("Build first (tools\\build.cmd Release), or restore the files above.", file=sys.stderr)
        return 1

    version = read_version()
    entries = collect_entries(version)
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    zip_path = out / f"LessonPlugins-{version}.zip"
    tmp_path = out / (zip_path.name + ".tmp")
    try:
        with zipfile.ZipFile(tmp_path, "w", zipfile.ZIP_DEFLATED) as zf:
            for name, src in entries:
                if isinstance(src, bytes):
                    zf.writestr(name, src)
                else:
                    zf.write(src, name)
        os.replace(tmp_path, zip_path)
    except BaseException:
        if tmp_path.exists():
            tmp_path.unlink()
        raise

    digest = sha256_of(zip_path)
    sha_path = out / (zip_path.name + ".sha256")
    sha_path.write_text(f"{digest}  {zip_path.name}\n", encoding="ascii")

    print(f"zip: {zip_path}")
    print(f"sha256 file: {sha_path}")
    print(f"size: {zip_path.stat().st_size} bytes")
    print(f"sha256: {digest}")
    print("files:")
    with zipfile.ZipFile(zip_path) as zf:
        for info in zf.infolist():
            print(f"  {info.file_size:>10}  {info.filename}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
