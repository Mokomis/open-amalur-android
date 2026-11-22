"""Compile the prepared Open Amalur guest and its native Windows host."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess

from build_native import REPO
from shaders import compile_shaders


WINDOWS_SOURCES = {
    "platform_android.o": "platform.c",
    "rt_host_android.o": "host.c",
    "rt_user32.o": "user32.c",
}
ANDROID_ONLY = {
    "android_native_app_glue.o", "native_app.o", "gamepad_android.o",
    "engine_core_affinity.o", "engine_hints_60.o",
    "veil_shadow_hook.o", "veil_transmog_trampolines.o",
    "veil_tablet_trampoline.o", "veil_dialogue_touch.o",
}

GUEST_ENTRIES = {
    "shard_193": ("00F7CFA0", "skin"),
    "shard_186": ("00F24EF0", "component"),
    "shard_205": ("01025FA0", "portrait_lookup"),
    "shard_184": ("00F046F0", "portrait_model"),
}
GUEST_CALLS = {
    "shard_193": (
        ("F7D01D", "00F5C2A0", "veil_preview_style_effects"),
        ("F7D2C0", "00F74BD0", "veil_preview_held_item"),
    ),
    "shard_152": (("D32D23", "00D13ED0", "veil_shadow_build_cascades"),),
    "shard_153": (("D4244D", "00CA0AA0", "veil_vegetation_select_cells"),),
    "shard_142": (("C9851A", "00C74280", "veil_occlusion_render"),),
    "shard_150": (
        ("D13F44", "00C43700", "veil_shadow_prepare_cascades"),
        ("D1116E", "00C6F160", "veil_depth_of_field_render"),
        ("D11221", "00C6F160", "veil_depth_of_field_render"),
    ),
    "shard_184": (("EFAE32", "01076440", "veil_windows_acquire_profile"),),
}
GUEST_SETTINGS = {
    "shard_138": (
        "extern unsigned veil_shadow_map_size;",
        "st32((uint32_t)(esi + 0xC4u), (uint32_t)(((uint32_t)0x400u)));",
        "st32((uint32_t)(esi + 0xC4u), veil_shadow_map_size);",
    ),
    "shard_139": (
        "extern double veil_depth_of_field_scale;",
        "v.q[0] = ld64((uint32_t)(0x14C7AD0u)); ctx->xmm[0] = v;",
        "v.d[0] = ldd(0x14C7AD0u) * veil_depth_of_field_scale; ctx->xmm[0] = v;",
    ),
    "shard_142": (
        "extern unsigned veil_occlusion_enabled;",
        "{ uint32_t ea = (uint32_t)(0x19661F5u);\n      uint32_t a = ld8(ea),",
        "{ uint32_t ea = (uint32_t)(0x19661F5u);\n      uint32_t a = veil_occlusion_enabled ? ld8(ea) : 0,",
    ),
    "shard_143": (
        "extern float veil_vegetation_scale;",
        "v.u[0] = ld32((uint32_t)(esi + 0x14Cu)); ctx->xmm[1] = v;",
        "v.f[0] = ldf(esi + 0x14Cu) * veil_vegetation_scale; ctx->xmm[1] = v;",
    ),
}
WINDOWS_SHARDS = GUEST_ENTRIES.keys() | GUEST_CALLS.keys() | GUEST_SETTINGS.keys() | {"shard_109"}


def prepare_windows_shard(source, output):
    stem = source.stem
    if stem not in WINDOWS_SHARDS:
        return source
    content = source.read_text(encoding="utf-8")
    if stem in GUEST_ENTRIES:
        address, handler = GUEST_ENTRIES[stem]
        original = f"void f_{address}(X86Ctx *X86_RESTRICT ctx)\n{{"
        if content.count(original) != 1:
            raise ValueError(f"Could not locate guest definition {address}")
        content = content.replace(original, f"void veil_stock_{handler}(X86Ctx *X86_RESTRICT ctx)\n{{")
    for return_pc, address, handler in GUEST_CALLS.get(stem, ()):
        original = f"sts32(esp, 0x{return_pc}u); X86_SPILL; f_{address}(ctx);"
        if content.count(original) != 1:
            raise ValueError(f"Could not locate guest shader or portrait call at {return_pc}")
        content = content.replace(original, f"sts32(esp, 0x{return_pc}u); X86_SPILL; {handler}(ctx);")
    prototypes = "".join(f"void {handler}(X86Ctx *);\n"
                         for handler in sorted({entry[2] for entry in GUEST_CALLS.get(stem, ())}))
    if stem in GUEST_SETTINGS:
        declaration, original, replacement = GUEST_SETTINGS[stem]
        if content.count(original) != 1:
            raise ValueError(f"Unexpected guest graphics setting in {source.name}")
        content = content.replace(original, replacement)
        prototypes += declaration + "\n"
    if stem == "shard_109":
        original = "    if (x86_cc(&F, CC_B)) goto L_00A65A8E;\n"
        if content.count(original) != 1:
            raise ValueError("Unexpected guest startup movie branch")
        content = content.replace(original, "")
    content = content.replace('#include "x86rt.h"\n', '#include "x86rt.h"\n' + prototypes, 1)
    staged = output / source.name
    staged.write_text(content, encoding="utf-8")
    return staged


def compile_object(compiler, source, target, flags):
    temporary = target.with_suffix(".building.o")
    log = target.with_suffix(".log")
    with log.open("w", encoding="utf-8") as diagnostics:
        result = subprocess.run(
            [compiler, *flags, "-c", source, "-o", temporary],
            stdout=diagnostics, stderr=subprocess.STDOUT,
        )
    if result.returncode:
        print(f"Compilation failed for {target.name}. See {log}", flush=True)
        return None
    temporary.replace(target)
    return target


def windows_flags(job):
    options = iter(job["flags"])
    for option in options:
        if option == "-I":
            include = next(options)
            if not include.startswith(("@ndk/", "native/dxvk/include/")):
                yield "-I"
                yield REPO / include
        elif option == "-DFAUDIO_ANDROID_PLATFORM":
            yield "-DFAUDIO_WIN32_PLATFORM"
        elif not option.startswith(("--target=", "-march=")) and option != "-fPIC":
            yield option


def compile_guest(compiler, translation, output, jobs, update_hooks=False):
    plan = json.loads((REPO / "native/build-plan.json").read_text(encoding="utf-8"))
    entries = [job for job in plan["objects"] if job["source"].startswith("@generated/")]
    if update_hooks:
        if not (output / "guest.rsp").is_file():
            raise ValueError("Compile the Windows guest before updating the host.")
        entries = [job for job in entries
                   if Path(job["output"]).stem in WINDOWS_SHARDS]
    includes = ["-I", translation / "generated/include-amalur"]

    def compile_one(job):
        target = output / job["output"]
        source = translation / "guest" / (target.stem + ".c")
        source = prepare_windows_shard(source, output)
        return compile_object(compiler, source, target, [*windows_flags(job), *includes])

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        objects = []
        for index, target in enumerate(pool.map(compile_one, entries), 1):
            objects.append(target)
            if index % 32 == 0 or index == len(entries):
                print(f"Compiled {index} of {len(entries)} guest objects", flush=True)
    if None in objects:
        raise SystemExit("The Windows guest did not compile.")
    if update_hooks:
        return
    (output / "guest.rsp").write_text(
        "\n".join(f'"{obj.as_posix()}"' for obj in objects) + "\n", encoding="utf-8",
    )


def compile_host(compiler, translation, output, ndk, jobs):
    plan = json.loads((REPO / "native/build-plan.json").read_text(encoding="utf-8"))
    entries = [job for job in plan["objects"]
               if not job["source"].startswith("@generated/") and job["output"] not in ANDROID_ONLY]
    windows = REPO / "native/windows"
    shader_headers = output / "shader-headers"
    if not shader_headers.is_dir():
        compile_shaders(REPO / "native/shaders", ndk, shader_headers)
    veil = REPO / "native/android"
    common = [
        "-D_WIN32_WINNT=0x0A00", "-DCOBJMACROS", "-I", windows / "include",
        "-I", shader_headers, "-I", translation / "generated/include-amalur",
        "-I", translation / "guest-helpers", "-I", REPO / "native/runtime",
        "-I", REPO / "native", "-I", veil / "app", "-I", veil / "veil-waterfall",
    ]
    for name in ("startup", "assets"):
        entries.append({"output": name + ".o", "source": f"native/windows/{name}.c", "flags": ["-O2"]})

    def compile_one(job):
        name = job["output"]
        source = windows / WINDOWS_SOURCES[name] if name in WINDOWS_SOURCES else REPO / job["source"]
        flags = [*common, *windows_flags(job)]
        helper = translation / "guest-helpers" / source.name
        if helper.is_file():
            flags += ["-iquote", source.parent]
            source = helper
        if name == "startup_probe.o":
            flags.append("-Dmain=oa_guest_main")
        output_file = output / name
        if "rename" not in job:
            return compile_object(compiler, source, output_file, flags)
        raw = output_file.with_suffix(".unrenamed.o")
        if not compile_object(compiler, source, raw, flags):
            return None
        old, new = job["rename"]
        subprocess.run([compiler.with_name("llvm-objcopy.exe"), "--redefine-sym",
                        f"{old}={new}", raw, output_file], check=True)
        raw.unlink()
        return output_file

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        objects = list(pool.map(compile_one, entries))
    if None in objects:
        raise SystemExit("The Windows host did not compile.")
    return objects


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--llvm-bin", type=Path, required=True)
    cli.add_argument("--translation", type=Path, required=True)
    cli.add_argument("--output", type=Path, required=True)
    cli.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    cli.add_argument("--ndk", type=Path, help="existing shader-tools installation")
    phase = cli.add_mutually_exclusive_group()
    phase.add_argument("--guest-only", action="store_true")
    phase.add_argument("--host-only", action="store_true")
    args = cli.parse_args()
    if args.jobs < 1:
        cli.error("--jobs must be positive")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = args.llvm_bin.resolve() / "x86_64-w64-mingw32-clang.exe"
    translation = args.translation.resolve()
    compile_guest(compiler, translation, output, args.jobs, update_hooks=args.host_only)
    if args.guest_only:
        return
    if args.ndk is None:
        cli.error("--ndk is required for the shared shader build")
    objects = compile_host(compiler, translation, output, args.ndk.resolve(), args.jobs)
    link = output / "host.rsp"
    link.write_text("\n".join(f'"{obj.as_posix()}"' for obj in objects) + "\n", encoding="utf-8")
    executable = output / "OpenAmalur.exe"
    subprocess.run([compiler, "@" + str(output / "guest.rsp"), "@" + str(link), "-o", executable,
                    "-static", "-pthread", "-mwindows", "-Wl,--stack,16777216", "-lwinmm", "-luser32", "-lgdi32",
                    "-lole32", "-ldxguid", "-luuid", "-ladvapi32"], check=True)
    print(f"Built {executable}", flush=True)


if __name__ == "__main__":
    main()
