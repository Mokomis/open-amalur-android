"""Build a Windows Veil overlay using a local game and the signed v0.1.9 APK."""

import argparse
from hashlib import file_digest, sha256
from pathlib import Path
from secrets import randbelow
import shutil
from struct import iter_unpack, pack, pack_into, unpack, unpack_from
from tempfile import NamedTemporaryFile
from zipfile import ZipFile
from zlib import crc32

from amalur_script import OPCODE_NAMES, parse_bytes
from build_native import require_input
from generate_veil_edits import index_of, read_entry
from mark_veil_portraits import constant_value
from fix_veil_menu_overlap import fix_batch

RELEASE_APK_SHA256 = "32e41b16c88e838cc2edae8476dcb460a9dc2b1fb87f9fa6af3479a59a795ff5"
OPTIONS_SCRIPT = 13523
TUTORIAL_SCRIPT = 733459
SCRIPT_BATCH_KEY = crc32(b"134230570_klua.batch")
ASSETS = (
    "veil-waterfall.bc3",
    "ambience/day-flicker.pcm", "ambience/day-grasshopper.pcm", "ambience/day-thrush.pcm",
    "ambience/night-crickets.pcm", "ambience/night-owl.pcm", "ambience/night-treefrog.pcm",
)


def windows_display_mode(script):
    def string(value):
        raw = value.encode("ascii") + b"\0"
        return pack(">I", len(raw)) + raw

    parsed = parse_bytes(script)
    children = parsed["chunk"]["root_function"]["children"]
    get = len(children)
    first, end = children[0]["file_offset"] - 4, children[-1]["end_file_offset"]
    callbacks = []
    for child in children[183:185]:
        callback = script[child["file_offset"]:child["end_file_offset"]]
        callback = callback.replace(string("VeilRenderResolution"), string("VeilWindowMode"))
        callback = callback.replace(string("m_veil_resolution_labels"), string("m_veil_window_mode_labels"))
        callbacks.append(callback)
    script = script[:first] + pack(">I", get + 2) + script[first + 4:end] + b"".join(callbacks) + script[end:]
    offset = parsed["wrapper"]["payload_file_offset"]
    script = script[:offset - 4] + pack("<I", len(script) - offset) + script[offset:]
    parsed = parse_bytes(script)
    root = parsed["chunk"]["root_function"]
    # Android's Touch Controls opens a launcher panel. Windows has no such
    # panel, so omit this menu item instead of leaving a dead action.
    touch = root["instructions"]
    touch_sites = [i for i, ins in enumerate(touch)
                   if ins["opcode_name"] == "LOADK"
                   and constant_value(root["constants"][ins["Bx_raw"]]) == "veil_touch_controls"]
    if (len(touch_sites) != 1
            or touch[touch_sites[0] + 3]["opcode_name"] != "LOADK"
            or constant_value(root["constants"][touch[touch_sites[0] + 3]["Bx_raw"]]) != "Touch Controls"
            or touch[touch_sites[0] + 13]["opcode_name"] != "CALL_I"):
        raise ValueError("Unexpected Touch Controls menu row")
    result = bytearray(script)
    pack_into(">I", result, touch[touch_sites[0] + 13]["file_offset"],
              (OPCODE_NAMES.index("MOVE") << 25))
    script = bytes(result)
    parsed = parse_bytes(script)
    root = parsed["chunk"]["root_function"]
    names = [constant_value(value) for value in root["constants"]]
    sites = [ins["pc"] for ins in root["instructions"]
             if ins["opcode_name"] == "LOADK" and names[ins["Bx_raw"]] == "veil_native_hardware_scaling"]
    if len(sites) != 1:
        raise ValueError("Unexpected Android hardware scaling row")
    pc = sites[0]
    instructions = root["instructions"]
    result = bytearray(script)

    def op(relative, name, a, b=0, c=0):
        pack_into(">I", result, instructions[pc + relative]["file_offset"],
                  (OPCODE_NAMES.index(name) << 25) | (b << 17) | (c << 8) | a)

    def load(relative, register, value):
        index = next(i for i, v in enumerate(names) if type(v) is type(value) and v == value)
        op(relative, "LOADK", register, index >> 9, index & 511)

    if instructions[pc + 35]["opcode_name"] != "CALL_I" or instructions[pc + 27]["opcode_name"] != "SETLIST":
        raise ValueError("Unexpected display mode row layout")
    load(4, 4, 1)  # First discrete choice: Windowed.
    op(6, "CLOSURE", 4, get >> 9, get & 511)
    op(9, "CLOSURE", 4, (get + 1) >> 9, (get + 1) & 511)
    load(17, 6, 2)  # Three choices in the integer range.
    # Fallbacks in the getter and active-value callback must also be Windowed.
    for child in (180, 182):
        fn = root["children"][child]
        start, end = fn["file_offset"], fn["end_file_offset"]
        old = bytes(result[start:end])
        fallback = b"\x03" + pack(">i", 4)
        if old.count(fallback) != 1:
            raise ValueError("Unexpected display mode callback default")
        result[start:end] = old.replace(fallback, b"\x03" + pack(">i", 0))

    # Replace the labels table without altering shared graphics constants.
    from mark_veil_portraits import Code, insert
    code = Code(root)
    code.op("LOADNIL", 4, 4)
    for name in ("int_range", "veil_active"):
        code.load(5, name)
        code.op("SETTABLE_S", 3, 5, 4)
    code.op("NEWTABLE", 4, 3)
    for register, value in enumerate((0, 1, 2), 5):
        code.load(register, value)
    code.op("SETLIST", 4, 3, 1)
    code.load(5, "params")
    code.op("SETTABLE_S", 3, 5, 4)
    code.op("NEWTABLE", 4, 3)
    for register, label in enumerate(("Windowed", "Borderless Fullscreen", "Fullscreen"), 5):
        code.load(register, label)
    code.op("SETLIST", 4, 3, 1)
    code.wide("SETGLOBAL", 4, code.index("m_veil_window_mode_labels"))
    code.load(5, "display_vals")
    code.op("SETTABLE_S", 3, 5, 4)
    start, end, replacement = insert(bytes(result), root, pc + 35, code, 10)
    result = bytes(result[:start]) + replacement + bytes(result[end:])
    for before, after in (("veil_native_hardware_scaling", "veil_window_mode"),
                          ("Android Hardware Scaling", "Window Mode (restart required)"),
                          ("VeilNativeHardwareScalingActive", "VeilWindowModeActive"),
                          ("VeilNativeHardwareScaling", "VeilWindowMode")):
        result = result.replace(string(before), string(after))
    offset = parsed["wrapper"]["payload_file_offset"]
    result = result[:offset - 4] + pack("<I", len(result) - offset) + result[offset:]
    parse_bytes(result)
    return result


def fix_tutorial_arguments(script):
    if sha256(script).hexdigest() != "6af1ac0771246a326493b89961f499e5e48c82d72d006d4a289cc6d4659b4789":
        raise ValueError("Unexpected Veil tutorial script")
    function = parse_bytes(script)["chunk"]["root_function"]["children"][1]
    condition = function["instructions"][272]
    # Veil runs additional code between loading args and testing them.
    # Read the argument register explicitly before selecting the formatter.
    result = bytearray(script)
    pack_into(">I", result, condition["file_offset"], (OPCODE_NAMES.index("TEST") << 25) | condition["A"])
    parse_bytes(result)
    return result


def patch_menu_batch(batch):
    batch = fix_batch(batch)
    count, = unpack_from("<I", batch)
    offset = 4 + count * 8
    headers, records = [], []
    matches = 0
    for key, size in iter_unpack("<II", batch[4:offset]):
        record = batch[offset:offset + size]
        if key == OPTIONS_SCRIPT:
            record = windows_display_mode(record)
            matches += 1
        elif key == TUTORIAL_SCRIPT:
            record = fix_tutorial_arguments(record)
            matches += 1
        headers.append(pack("<II", key, len(record)))
        records.append(record)
        offset += size
    if offset != len(batch) or matches != 2:
        raise ValueError("Malformed Veil menu batch")
    return batch[:4] + b"".join(headers) + b"".join(records)


def patch_archive(patch, initial, recipe, output, platform="windows"):
    if platform not in ("windows", "android"):
        raise ValueError("Unknown Veil platform")
    sources = (patch, initial)
    source_indexes = [index_of(archive) for archive in sources]
    directory = dict(source_indexes[0])
    if recipe.read(8) != b"VEILED01":
        raise ValueError("Unknown Veil recipe")
    count, = unpack(">I", recipe.read(4))
    if not 0 < count <= 128:
        raise ValueError("Invalid Veil recipe record count")
    patch.seek(0)
    shutil.copyfileobj(patch, output, 1024 * 1024)
    found_options = False
    for _ in range(count):
        key, archive, old_size, new_size = unpack(">IBII", recipe.read(13))
        before, after = recipe.read(32), recipe.read(32)
        if not 0 < old_size <= 64 * 1024 * 1024 or not 0 < new_size <= 64 * 1024 * 1024:
            raise ValueError("Invalid Veil recipe record size")
        source = read_entry(sources[archive], source_indexes[archive][key])
        if len(source) != old_size or sha256(source).digest() != before:
            raise ValueError("The game archives do not match the supported Veil release")
        record = bytearray()
        while (command := recipe.read(1)) != b"\0":
            start, size = unpack(">II", recipe.read(8))
            if start + size > old_size or len(record) + size > new_size:
                raise ValueError("Invalid Veil edit bounds")
            segment = source[start:start + size]
            if command == b"\x01":
                record.extend(segment)
            elif command == b"\x02":
                record.extend(a ^ b for a, b in zip(segment, recipe.read(size), strict=True))
            else:
                raise ValueError("Unknown Veil edit")
        if len(record) != new_size or sha256(record).digest() != after:
            raise ValueError("Could not reproduce a Veil record")
        if key == SCRIPT_BATCH_KEY and platform == "windows":
            record = patch_menu_batch(record)
            found_options = True
        output.write(bytes(-output.tell() % 16))
        directory[key] = (output.tell() // 16, len(record))
        output.write(pack("<II", len(record), 0))
        output.write(record)
    if recipe.read(1) or (platform == "windows" and not found_options):
        raise ValueError("The recipe must contain the Windows-adjusted Veil menu")
    offset = output.tell()
    output.write(pack("<II", len(directory), 0))
    for key, (location, size) in sorted(directory.items()):
        output.write(pack("<III", key, location, size))
    output.seek(16)
    output.write(pack("<Q", offset))


def prepare_profile(profile):
    profile_file = profile / "offline-profile.bin"
    if not profile_file.exists():
        identity = 0x0110000100000000 | (1 + randbelow(0x7fffffff))
        try:
            with profile_file.open("xb") as output:
                output.write(pack("<8sQII7s", b"OAOFF01\0", identity, 0, 7, b"english"))
        except FileExistsError:
            pass


def install(game, profile, package):
    require_input(game / "koa.exe", "amalur")
    require_input(game / "binkw32.dll", "bink")
    with package.open("rb") as signed:
        if file_digest(signed, "sha256").hexdigest() != RELEASE_APK_SHA256:
            raise ValueError("Use the published Open Amalur v0.1.10 APK")
    destination = profile / "runtime/overlay/data/patch_0.pak"
    destination.parent.mkdir(parents=True, exist_ok=True)
    assets = profile / "assets"
    with ZipFile(package) as release, (game / "data/patch_0.pak").open("rb") as patch, (
        game / "data/initial_0.pak"
    ).open("rb") as initial, release.open("assets/local-build/veil-edits.bin") as recipe:
        with NamedTemporaryFile(dir=destination.parent, delete=False) as temporary:
            stage = Path(temporary.name)
            try:
                patch_archive(patch, initial, recipe, temporary)
                temporary.close()
                for name in ASSETS:
                    target = assets / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with release.open("assets/" + name) as source, target.open("wb") as output:
                        shutil.copyfileobj(source, output)
                stage.replace(destination)
            finally:
                temporary.close()
                stage.unlink(missing_ok=True)
    prepare_profile(profile)
    print(f"Installed Veil into {profile}", flush=True)


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--game-dir", type=Path, required=True)
    cli.add_argument("--profile", type=Path, required=True)
    cli.add_argument("--apk", type=Path, required=True)
    args = cli.parse_args()
    install(args.game_dir.resolve(), args.profile.resolve(), args.apk.resolve())


if __name__ == "__main__":
    main()
