"""Build a Windows Veil overlay using a local game and the signed v0.1.8 APK."""

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

RELEASE_APK_SHA256 = "4db7f4e0cdec6c5edd1fca1c04d1351d22a1275f415a3369f18cdaa3159a5624"
OPTIONS_SCRIPT = 13523
TUTORIAL_SCRIPT = 733459
SCRIPT_BATCH_KEY = crc32(b"134230570_klua.batch")
ASSETS = (
    "veil-waterfall.bc3",
    "ambience/day-flicker.pcm", "ambience/day-grasshopper.pcm", "ambience/day-thrush.pcm",
    "ambience/night-crickets.pcm", "ambience/night-owl.pcm", "ambience/night-treefrog.pcm",
)


def remove_android_scaling(script):
    root = parse_bytes(script)["chunk"]["root_function"]
    names = [constant_value(value) for value in root["constants"]]
    option = names.index("veil_native_hardware_scaling")
    sites = [ins["pc"] for ins in root["instructions"]
             if ins["opcode_name"] == "LOADK" and ins["Bx_raw"] == option]
    if len(sites) != 1:
        raise ValueError("Unexpected Android hardware scaling row")
    # The verified row constructs its table locally, then inserts it into the
    # Resolution list. Skip that insert without shifting subsequent jumps.
    tail = root["instructions"][sites[0] + 1:sites[0] + 48]
    insert = next(ins for ins in tail if ins["opcode_name"] == "CALL_I")
    expected = (OPCODE_NAMES.index("CALL_I") << 25) | (4 << 17) | (1 << 8)
    if int(insert["word_hex"], 16) != expected:
        raise ValueError("Unexpected Android hardware scaling insert")
    result = bytearray(script)
    pack_into(">I", result, insert["file_offset"], OPCODE_NAMES.index("MOVE") << 25)
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
    count, = unpack_from("<I", batch)
    offset = 4 + count * 8
    result = bytearray(batch)
    matches = 0
    for key, size in iter_unpack("<II", batch[4:offset]):
        if key == OPTIONS_SCRIPT:
            result[offset:offset + size] = remove_android_scaling(batch[offset:offset + size])
            matches += 1
        elif key == TUTORIAL_SCRIPT:
            result[offset:offset + size] = fix_tutorial_arguments(batch[offset:offset + size])
            matches += 1
        offset += size
    if offset != len(batch) or matches != 2:
        raise ValueError("Malformed Veil menu batch")
    return result


def patch_archive(patch, initial, recipe, output):
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
        if key == SCRIPT_BATCH_KEY:
            record = patch_menu_batch(record)
            found_options = True
        output.write(bytes(-output.tell() % 16))
        directory[key] = (output.tell() // 16, len(record))
        output.write(pack("<II", len(record), 0))
        output.write(record)
    if recipe.read(1) or not found_options:
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
            raise ValueError("Use the published Open Amalur v0.1.8 APK")
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
