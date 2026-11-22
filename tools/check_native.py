"""Syntax-check the hand-written native sources with the Android NDK.

Uses the compile flags from native/build-plan.json, so include paths and
macros match a real engine build. Translated guest sources and generated
shader headers come from the game files and are not checked; shader headers
are replaced with stubs. Needs only the NDK, so it runs on any host.

    python tools/check_native.py --ndk <android-ndk> [name.c ...]
"""

import argparse
import json
from pathlib import Path
import platform
import subprocess
import sys
import tempfile

from shaders import SHADERS

REPO = Path(__file__).resolve().parents[1]


def clang_path(ndk):
    host = {"Darwin": "darwin-x86_64", "Linux": "linux-x86_64", "Windows": "windows-x86_64"}[platform.system()]
    clang = ndk / "toolchains/llvm/prebuilt" / host / "bin" / ("clang.exe" if host.startswith("windows") else "clang")
    if not clang.is_file():
        raise SystemExit(f"clang not found at {clang}")
    return clang


def write_shader_stubs(folder):
    for header, shaders in SHADERS.items():
        (folder / header).write_text(
            "".join(f"static const unsigned int {symbol}[] = {{0x07230203u}};\n" for _, symbol in shaders),
            encoding="utf-8")


def main():
    cli = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    cli.add_argument("--ndk", type=Path, required=True)
    cli.add_argument("names", nargs="*", help="only check sources with these file names")
    args = cli.parse_args()
    clang = clang_path(args.ndk)
    plan = json.loads((REPO / "native/build-plan.json").read_text(encoding="utf-8"))
    # Sources rebuilt from lifted game code at build time (see build_native.py).
    derived = set(json.loads((REPO / "native/derived-edits.json").read_text(encoding="utf-8")))
    jobs = [job for job in plan["objects"] if not job["source"].startswith(("@generated/", "@ndk/"))
            and Path(job["source"]).name not in derived and job["output"] != "rcpc_copy_runtime.o"
            and (not args.names or Path(job["source"]).name in args.names)]
    failures = 0
    with tempfile.TemporaryDirectory() as temporary:
        stubs = Path(temporary)
        write_shader_stubs(stubs)
        for job in jobs:
            flags = list(job["flags"])
            for index, option in enumerate(flags[:-1]):
                if option == "-I":
                    path = flags[index + 1]
                    flags[index + 1] = str(args.ndk / path.split("/", 1)[1]) if path.startswith("@ndk/") \
                        else str(REPO / path)
            source = REPO / job["source"]
            result = subprocess.run([str(clang), *flags, "-I", str(stubs), "-fsyntax-only", str(source)],
                                    capture_output=True, text=True)
            if result.returncode:
                failures += 1
                print(f"FAIL {job['source']}\n{result.stderr.strip()}\n", flush=True)
            else:
                print(f"ok   {job['source']}", flush=True)
    print(f"{len(jobs) - failures} of {len(jobs)} native sources passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
