"""Build the installer LessonPlugins-<version>-setup.exe with Inno Setup.

Usage: python tools/package-exe.py [--out <folder>]   (default: dist/ under the repository root)
Runs package.py for the licenses and the version, so the zip is written next to the installer. Needs Inno Setup 6
(winget install JRSoftware.InnoSetup). Build first with tools\build.cmd Release.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CANDIDATES = [
    Path(os.environ.get("LOCALAPPDATA", "")) / "Programs/Inno Setup 6/ISCC.exe",
    Path("C:/Program Files (x86)/Inno Setup 6/ISCC.exe"),
    Path("C:/Program Files/Inno Setup 6/ISCC.exe"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=ROOT / "dist")
    out = ap.parse_args().out.resolve()
    iscc = next((p for p in CANDIDATES if p.is_file()), None)
    if iscc is None:
        print("ISCC.exe not found. Install Inno Setup 6: winget install JRSoftware.InnoSetup", file=sys.stderr)
        return 1
    rc = subprocess.run([sys.executable, str(ROOT / "tools/package.py"), "--out", str(out)], stdout=subprocess.DEVNULL).returncode
    if rc:
        return rc
    zips = sorted(out.glob("LessonPlugins-*.zip"), key=lambda p: p.stat().st_mtime)
    zip_path = zips[-1]
    version = zip_path.stem.split("-", 1)[1]
    with tempfile.TemporaryDirectory() as tmp:
        lic = Path(tmp) / "licenses"
        with zipfile.ZipFile(zip_path) as zf:
            for name in zf.namelist():
                if "/licenses/" in name and not name.endswith("/"):
                    lic.mkdir(exist_ok=True)
                    (lic / Path(name).name).write_bytes(zf.read(name))
        cmd = [str(iscc), "/Q", f"/DAppVersion={version}", f"/DRoot={ROOT}", f"/DOutDir={out}", f"/DLicDir={lic}",
               str(ROOT / "tools/dist/LessonPlugins.iss")]
        rc = subprocess.run(cmd).returncode
    if rc:
        return rc
    exe = out / f"LessonPlugins-{version}-setup.exe"
    import hashlib
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    (out / (exe.name + ".sha256")).write_text(f"{digest}  {exe.name}\n", encoding="ascii")
    print(f"exe: {exe}\nsize: {exe.stat().st_size} bytes\nsha256: {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
