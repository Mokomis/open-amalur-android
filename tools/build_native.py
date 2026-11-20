"""Build the Android ARM64 library from the player's local Windows binaries."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from hashlib import file_digest
import json
import os
from pathlib import Path
import subprocess
import sys

from patch_linked_library import install_veil_hooks
from prepare_translation import prepare, prepare_helpers
from shaders import compile_shaders

REPO = Path(__file__).resolve().parents[1]
LOCAL_SHA = {
    "amalur": "16a400f6e8fc10dbe446a9e57717de9fbe975ef17c004f4ca405e2e4e09cb314",
    "bink": "36810b3318acd07a5b4e78fe72db163ed63880ab2eb2d9b5bda0331ded32a9c3",
}
TOOLCHAIN = "toolchains/llvm/prebuilt/windows-x86_64/bin"


def checksum(path):
    with path.open("rb") as stream:
        return file_digest(stream, "sha256").hexdigest()


def require_input(file, key):
    if checksum(file) != LOCAL_SHA[key]:
        raise ValueError(f"{file.name} does not match the expected 32-bit build for {key}")


def translate(args, plan):
    games = {
        "amalur": args.game_dir / "koa.exe",
        "bink": args.game_dir / "binkw32.dll",
    }
    for name, source in games.items():
        require_input(source, name)
    args.output.mkdir(parents=True)
    output = args.output / "generated"
    output.mkdir()

    def generate(pair):
        module, source = pair
        receipt = output / f"{module}.log"
        with receipt.open("w", encoding="utf-8") as log:
            command = [sys.executable, "-u", REPO / "translator/translate.py", module, source,
                       "--llvm", args.llvm_bin, "--output", output / module,
                       "--include-dir", output / f"include-{module}"]
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f"Translation failed. See {receipt}")
        print(f"Translated {module}", flush=True)

    with ThreadPoolExecutor(max_workers=len(games)) as pool:
        list(pool.map(generate, games.items()))
    prepare(output, args.output / "guest", plan["objects"])
    prepare_helpers(output, args.output / "guest-helpers")


def compile_and_link(args, plan):
    with (args.ndk / "source.properties").open() as properties:
        if "Pkg.Revision = 30.0.16248370" not in properties.read():
            raise ValueError("The current game-specific hooks require Android NDK 30.0.16248370.")
    clang = args.ndk / TOOLCHAIN / "clang.exe"
    shader_headers = args.output / "shader-headers"
    compile_shaders(REPO / "native/shaders", args.ndk, shader_headers)
    folder = args.output / "objects"
    folder.mkdir()
    logs = args.output / "compile-logs"
    logs.mkdir()

    def resolve(path):
        if path.startswith("@ndk/"):
            return args.ndk / path.split("/", 1)[1]
        return REPO / path

    def compile_one(job):
        name = job["output"]
        output = folder / name
        flags = list(job["flags"])
        for index, option in enumerate(flags[:-1]):
            if option == "-I":
                flags[index + 1] = resolve(flags[index + 1])
        if job["source"].startswith("@generated/"):
            source = args.output / "guest" / (output.stem + ".c")
        else:
            source = resolve(job["source"])
            generated_helper = args.output / "guest-helpers" / source.name
            if generated_helper.is_file():
                flags += ["-iquote", source.parent]
                source = generated_helper
            if name == "rcpc_copy_runtime.o":
                flags += ["-I", args.output / "guest-helpers"]
        flags += ["-I", args.output / "generated/include-amalur", "-I", shader_headers]
        if name == "oa_vk_android_pass_batch.o":
            if args.poco_panvk_adrenotools:
                flags += ["-DOA_POCO_PANVK", "-I", args.poco_panvk_adrenotools / "include"]
            elif args.retroid_turnip_adrenotools:
                flags += ["-DOA_RETROID_TURNIP", "-I", args.retroid_turnip_adrenotools / "include"]
        raw = output.with_suffix(".before-rename.o") if "rename" in job else output
        log_path = logs / (name + ".log")
        with log_path.open("w", encoding="utf-8") as log:
            result = subprocess.run([clang, *flags, "-c", source, "-o", raw],
                                    stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f"Compilation failed for {name}. See {log_path}")
        if "rename" in job:
            previous, symbol = job["rename"]
            subprocess.run([clang.with_name("llvm-objcopy.exe"), "--redefine-sym",
                            f"{previous}={symbol}", raw, output], check=True)
            raw.unlink()
        return output

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        objects = []
        for result in pool.map(compile_one, plan["objects"]):
            objects.append(result)
            if len(objects) % 32 == 0:
                print(f"Compiled {len(objects)} of {len(plan['objects'])} objects", flush=True)
    response = args.output / "objects.rsp"
    response.write_text("\n".join(f'"{obj.as_posix()}"' for obj in objects) + "\n", encoding="utf-8")
    library = args.output / "liboaoriginal.so"
    subprocess.run([clang, *plan["link"][:2], "@" + str(response), "-o", library, *plan["link"][2:]], check=True)
    install_veil_hooks(library, clang, REPO / "native/hooks/startup_input.s", args.output)
    print(f"{checksum(library)}  {library}", flush=True)


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--game-dir", type=Path, help="folder containing koa.exe and binkw32.dll")
    cli.add_argument("--llvm-bin", type=Path, help="directory containing the LLVM-mingw libLLVM-23.dll")
    cli.add_argument("--ndk", type=Path)
    driver = cli.add_mutually_exclusive_group()
    driver.add_argument("--retroid-turnip-adrenotools", type=Path,
                        help="libadrenotools checkout for the Retroid Pocket 5 test renderer")
    driver.add_argument("--poco-panvk-adrenotools", type=Path,
                        help="libadrenotools checkout for the Poco X7 Pro test renderer")
    cli.add_argument("--output", type=Path, required=True, help="new local build directory")
    cli.add_argument("--jobs", type=int, default=min(6, os.cpu_count() or 1))
    phase = cli.add_mutually_exclusive_group()
    phase.add_argument("--prepare-only", action="store_true")
    phase.add_argument("--compile-only", action="store_true", help="compile an existing prepared build")
    args = cli.parse_args()
    if os.name != "nt":
        cli.error("this build uses the Windows x86-64 LLVM and Android toolchains")
    if not args.compile_only and None in (args.game_dir, args.llvm_bin):
        cli.error("--game-dir and --llvm-bin are needed for translation")
    if not args.prepare_only and args.ndk is None:
        cli.error("--ndk is needed for compilation")
    if args.jobs < 1:
        cli.error("--jobs must be positive")
    args.output = args.output.resolve()
    plan = json.loads((REPO / "native/build-plan.json").read_text(encoding="utf-8"))
    if not args.compile_only:
        translate(args, plan)
    if not args.prepare_only:
        compile_and_link(args, plan)


if __name__ == "__main__":
    main()
