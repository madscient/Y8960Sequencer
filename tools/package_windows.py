#!/usr/bin/env python3
"""Windows 版の配布 zip を作る。

3つのエミュレータとこのリポジトリを、CRT を静的リンクした Release でビルドし、
実行ファイル・ライブラリ・ライセンス文を1つの zip にまとめる。

    python tools/package_windows.py v0.1.0

エミュレータのリポジトリは、このリポジトリと同じフォルダに並んでいる前提。
出力は build/release/ の下。始める前に build/release/ を消す。
"""

import os
import shutil
import string
import subprocess
import sys
import zipfile
from pathlib import Path

EMULATORS = ["Y8960emu", "EPSGemuEngine", "DSAemuEngine"]
LIBRARIES = ["Y8960emuEngine.dll", "EPSGemuEngine.dll", "DSAemuEngine.dll"]
THIRD_PARTY = [
    ("Y8960emu", "extern/ymfm/LICENSE", "ymfm.txt"),
    ("EPSGemuEngine", "extern/ay8910/LICENSE", "ay8910.txt"),
    ("EPSGemuEngine", "extern/mpeg_audio/LICENSE", "mpeg_audio.txt"),
    ("EPSGemuEngine", "extern/ymz280b/LICENSE", "ymz280b.txt"),
    ("DSAemuEngine", "extern/emu2149/LICENSE", "emu2149.txt"),
    ("DSAemuEngine", "extern/emu2212/LICENSE", "emu2212.txt"),
    ("DSAemuEngine", "extern/emu2413/LICENSE", "emu2413.txt"),
    ("DSAemuEngine", "extern/emu76489/LICENSE", "emu76489.txt"),
    ("DSAemuEngine", "extern/emu8950/LICENSE", "emu8950.txt"),
]
STATIC_CRT = "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"


def run(args, **kw):
    print("+", " ".join(str(a) for a in args), flush=True)
    subprocess.run([str(a) for a in args], check=True, **kw)


def free_drive():
    used = {p[0].upper() for p in os.listdrives()} if hasattr(os, "listdrives") else set()
    for letter in reversed(string.ascii_uppercase[3:]):
        if letter not in used and not Path(f"{letter}:\\").exists():
            return f"{letter}:"
    sys.exit("no free drive letter for subst")


def describe(repo):
    head = subprocess.run(["git", "-C", str(repo), "log", "-1", "--format=%h"],
                          capture_output=True, text=True, check=True).stdout.strip()
    dirty = subprocess.run(["git", "-C", str(repo), "status", "--porcelain"],
                           capture_output=True, text=True, check=True).stdout.strip()
    return head, bool(dirty)


def leaked_paths(files, needles):
    hits = []
    for f in files:
        data = f.read_bytes().lower()
        for n in needles:
            if n in data:
                hits.append(f.name)
                break
    return hits


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: package_windows.py <version tag>")
    version = sys.argv[1]
    repo = Path(__file__).resolve().parent.parent
    root = repo.parent

    for name in [repo.name] + EMULATORS:
        head, dirty = describe(root / name)
        print(f"{name}: {head}{' (uncommitted changes)' if dirty else ''}")
        if dirty:
            sys.exit(f"{name} has uncommitted changes")

    # 前のビルドが実際のパスで構成されていると、同じフォルダを別のパスから
    # 構成し直せない。
    shutil.rmtree(repo / "build" / "release", ignore_errors=True)

    # __FILE__ と PDB の場所がバイナリに残る。ビルドするフォルダの実際のパスには
    # 利用者の名前が入りうるので、subst で割り当てたドライブの上でビルドする。
    drive = free_drive()
    run(["subst", drive, root])
    try:
        v = Path(drive + "\\")
        out = v / repo.name / "build" / "release"
        emu = out / "emu"
        emu.mkdir(parents=True, exist_ok=True)
        for name in EMULATORS:
            run(["cmake", "-S", v / name, "-B", out / name, STATIC_CRT])
            run(["cmake", "--build", out / name, "--config", "Release"])
            for dll in (out / name).rglob("*.dll"):
                if dll.name in LIBRARIES and "Release" in dll.parts:
                    (emu / dll.name).write_bytes(dll.read_bytes())
        player = out / "player"
        run(["cmake", "-S", v / repo.name, "-B", player, STATIC_CRT,
             f"-DY8960_EMULATOR_DIR={emu}"])
        run(["cmake", "--build", player, "--config", "Release"])
        run(["ctest", "--test-dir", player, "-C", "Release", "--output-on-failure"])
    finally:
        run(["subst", drive, "/d"])

    out = repo / "build" / "release"
    bin_dir = out / "player" / "bin" / "Release"
    stem = f"y8960sequencer-{version}-windows-x64"
    files = {n: bin_dir / n for n in ["y8960player.exe", "y8960gui.exe"] + LIBRARIES}
    files["README.md"] = repo / "README.md"
    files["LICENSE"] = repo / "LICENSE"
    for name in EMULATORS:
        files[f"licenses/{name}.txt"] = root / name / "LICENSE"
    for name, src, dst in THIRD_PARTY:
        files[f"licenses/{dst}"] = root / name / src
    deps = out / "player" / "_deps"
    files["licenses/SDL3.txt"] = deps / "sdl3-src" / "LICENSE.txt"
    files["licenses/DearImGui.txt"] = deps / "imgui-src" / "LICENSE.txt"

    # 検査する語をここに書くと、このファイルが漏らしたくない値を持つことになる。
    # 実行している環境から組み立てる。
    needles = set()
    for p in [Path.home(), root]:
        for form in [str(p), str(p).replace("\\", "/")]:
            needles.add(form.lower().encode())
    binaries = [f for n, f in files.items() if n.endswith((".exe", ".dll"))]
    hits = leaked_paths(binaries, needles)
    if hits:
        sys.exit("local paths found in: " + ", ".join(hits))

    archive = out / f"{stem}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for name, src in sorted(files.items()):
            z.write(src, f"{stem}/{name}")
    print(f"wrote {archive}")


if __name__ == "__main__":
    main()
