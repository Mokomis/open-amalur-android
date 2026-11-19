"""Add the Android Dynamic Resolution switch to Veil's native Graphics menu."""
import argparse
from pathlib import Path
from struct import iter_unpack, pack, unpack_from

from amalur_script import parse_bytes
from mark_veil_portraits import Code, constant_value, insert
from patch_veil_environment import add_option_toggle, append_callbacks, repack_script


def patch_options(data):
    key = "VeilDynamicResolution"
    if key.encode("ascii") in data:
        raise ValueError("Dynamic Resolution is already installed")
    parsed = parse_bytes(data)
    root = parsed["chunk"]["root_function"]
    constants = [constant_value(c) for c in root["constants"]]
    if not {"Graphics", "m_options_list", "m_veil_toggle_labels"}.issubset(
            value for value in constants if isinstance(value, str)):
        raise ValueError("Expected the Veil Graphics menu and environment options")
    if root["instructions"][-1]["opcode_name"] != "RETURN":
        raise ValueError("Unrecognized Veil options initialization")

    data, getter = append_callbacks(data, key)
    parsed = parse_bytes(data)
    root = parsed["chunk"]["root_function"]
    registers = root["maximum_stack_size"]
    code = Code(root)
    add_option_toggle(code, registers, key, "Dynamic Resolution (60 FPS)", getter, "Graphics", 1)
    start, end, replacement = insert(data, root, len(root["instructions"]) - 1, code, registers + 6)
    return repack_script(data, parsed, data[:start] + replacement + data[end:])


def patch_batch(data):
    count, = unpack_from("<I", data)
    offset = 4 + count * 8
    directory, scripts = [], []
    found = False
    for key, size in iter_unpack("<II", data[4:offset]):
        script = data[offset:offset + size]
        offset += size
        if key == 13523:
            script = patch_options(script)
            parse_bytes(script)
            found = True
        directory.append(pack("<II", key, len(script)))
        scripts.append(script)
    if not found or offset != len(data):
        raise ValueError("Missing or incomplete Veil Graphics menu")
    return data[:4] + b"".join(directory) + b"".join(scripts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("batch", type=Path)
    args = parser.parse_args()
    args.batch.write_bytes(patch_batch(args.batch.read_bytes()))


if __name__ == "__main__":
    main()
