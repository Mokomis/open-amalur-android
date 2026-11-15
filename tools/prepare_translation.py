"""Apply the Android runtime's instrumentation and retained source changes."""
from hashlib import sha256
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
PROBE_DECLARATION = "void oa_probe_enter(X86Ctx*, uint32_t);"
PROBE_INCLUDE = '#include "oa_probe_fast.h"'
GUEST_FUNCTION = re.compile(r"(void f_([0-9A-F]{8})\(X86Ctx \*[^)]*\)\s*\{)")
GEOMETRY = {
    "shard_062.c": ("0078B1D0",),
    "shard_063.c": ("0078BDE0", "0078E190"),
}


def instrument_source(source, module, filename):
    if module != "compiler43" and not filename.startswith("shard_"):
        return source
    prototype = PROBE_DECLARATION
    if module == "compiler43":
        prototype = "void oa_probe_enter(X86Ctx*,uint32_t);"
    else:
        prototype += "\n"
    source = source.replace('#include "x86rt.h"\n', '#include "x86rt.h"\n' + prototype + "\n", 1)
    return GUEST_FUNCTION.sub(lambda match: match[1] + f"\n    oa_probe_enter(ctx,0x{match[2]}u);", source)


def keep_xmm_local(function):
    registers = sorted(set(re.findall(r"ctx->xmm\[(\d)\]", function)))
    result = re.sub(r"ctx->xmm\[(\d)\]", r"xmm\1", function)
    init = "\n".join(f"    X86Xmm xmm{i} = ctx->xmm[{i}];" for i in registers)
    spill = " ".join(f"ctx->xmm[{i}] = xmm{i};" for i in registers)
    reload = " ".join(f"xmm{i} = ctx->xmm[{i}];" for i in registers)
    if (result.count("X86_SPILL;") != result.count("X86_RELOAD;") + result.count("return;")
            or any(token in result for token in ("x86_dispatch(", "x86_trap(", "x87_", "longjmp", "return "))):
        raise ValueError("Unexpected guest geometry call boundary")
    result = result.replace("    X86_LOCALS;\n", "    X86_LOCALS;\n" + init + "\n", 1)
    return result.replace("X86_SPILL;", spill + " X86_SPILL;").replace("X86_RELOAD;", "X86_RELOAD; " + reload)


def geometry_source(source, filename):
    for address in GEOMETRY[filename]:
        pattern = re.compile(r"^void f_" + address + r"\(X86Ctx \*X86_RESTRICT ctx\)\n\{.*?^}", re.M | re.S)
        source, count = pattern.subn(lambda match: keep_xmm_local(match[0]), source)
        if count != 1:
            raise ValueError(f"No unique guest geometry function {address}")
    return source.replace(PROBE_DECLARATION, PROBE_INCLUDE, 1)


def apply_source_edits(source, recipe):
    lines = source.splitlines(keepends=True)
    for offset, removed, expected, lines_to_insert in reversed(recipe):
        original = "".join(lines[offset:offset + removed])
        if sha256(original.encode()).hexdigest() != expected:
            raise ValueError(f"Guest source changed at line {offset + 1}")
        replacement = "".join(original[part[0]:part[1]] if isinstance(part, list) else part
                              for part in lines_to_insert)
        lines[offset:offset + removed] = replacement.splitlines(keepends=True)
    return "".join(lines)


def prepare(root, destination, objects):
    changes = json.loads((ROOT / "native/guest-edits.json").read_text(encoding="utf-8"))
    destination.mkdir()
    for job in objects:
        item = job["source"]
        if not item.startswith("@generated/"):
            continue
        module, filename = item.split("/")[1:]
        path = root / module / filename
        source = instrument_source(path.read_text(encoding="utf-8"), module, filename)
        if job["inline"]:
            source = source.replace(PROBE_DECLARATION, PROBE_INCLUDE, 1)
        else:
            if filename in GEOMETRY:
                source = geometry_source(source, filename)
            else:
                source = apply_source_edits(source, changes[job["output"]])
        (destination / (Path(job["output"]).stem + ".c")).write_text(source, encoding="utf-8")


def prepare_helpers(root, destination):
    """Rebuild helpers that reuse locally lifted function bodies."""
    recipes = json.loads((ROOT / "native/derived-edits.json").read_text(encoding="utf-8"))
    destination.mkdir()
    for filename, recipe in recipes.items():
        functions = []
        for shard, address in recipe["functions"]:
            source = instrument_source((root / "amalur" / shard).read_text(encoding="utf-8"),
                                       "amalur", shard)
            pattern = r"^void f_" + address + r"\(X86Ctx \*X86_RESTRICT ctx\)\n\{.*?^}"
            match = re.search(pattern, source, re.M | re.S)
            if match is None:
                raise ValueError(f"No guest function {address} for {filename}")
            functions.append(match[0])
        original = "\n\n".join(functions)
        if sha256(original.encode()).hexdigest() != recipe["input_sha256"]:
            raise ValueError(f"Unexpected local function body for {filename}")
        result = "".join(original[part[0]:part[1]] if isinstance(part, list) else part
                         for part in recipe["edits"])
        if sha256(result.encode()).hexdigest() != recipe["output_sha256"]:
            raise ValueError(f"Could not reconstruct {filename}")
        (destination / filename).write_text(result, encoding="utf-8")
