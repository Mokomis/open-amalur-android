"""Add the Android Dynamic Resolution and heat-response switches to Veil's native Graphics menu."""
import argparse
from pathlib import Path
from struct import iter_unpack, pack, unpack_from

from amalur_script import parse_bytes
from fit_veil_inventory import insert_callback
from mark_veil_portraits import Code, constant_value, insert
from patch_veil_environment import add_option_toggle, append_callbacks, repack_script


DYNAMIC = "VeilDynamicResolution"
HEAT = "VeilResolutionHeatResponse"


def patch_released_toggle_template(data, root):
    # The released menu already has the controller and touch pill selector.
    # Direct the new row to the same template branch as its parent.
    template = root["children"][112]
    constants = [constant_value(c) for c in template["constants"]]
    if DYNAMIC not in constants or HEAT in constants:
        return data  # A fresh menu gets both entries from veil_gameplay_choices.
    ops = template["instructions"]
    site, = (op["pc"] for op in ops if op["opcode_name"] == "EQ"
             and op["C_raw"] == 256 + constants.index(DYNAMIC))
    if ops[site]["A"] != 1 or ops[site + 1]["opcode_name"] != "JMP":
        raise ValueError("Unrecognized Graphics toggle selector")

    def select_heat(fn):
        code = Code(fn)
        code.op("EQ", 1, ops[site]["B_raw"], 256 + code.index(HEAT))
        code.wide("JMP", 0, 65535 + 1)  # Skip the parent EQ and use its JMP.
        return code, fn["maximum_stack_size"]
    return insert_callback(data, 112, site, select_heat)


def patch_options(data):
    parsed = parse_bytes(data)
    root = parsed["chunk"]["root_function"]
    constants = [constant_value(c) for c in root["constants"]]
    if HEAT in constants:
        raise ValueError("Heat response is already installed")
    if not {"Graphics", "m_options_list", "m_veil_toggle_labels"}.issubset(
            value for value in constants if isinstance(value, str)):
        raise ValueError("Expected the Veil Graphics menu and environment options")
    if root["instructions"][-1]["opcode_name"] != "RETURN":
        raise ValueError("Unrecognized Veil options initialization")

    rows = []
    if DYNAMIC not in constants:
        rows.append((DYNAMIC, "Dynamic Resolution (60 FPS)"))
    else:
        data = patch_released_toggle_template(data, root)
    rows.append((HEAT, "Reduce Resolution When Hot"))
    callbacks = []
    for key, label in rows:
        data, getter = append_callbacks(data, key)
        callbacks.append((key, label, getter))
    parsed = parse_bytes(data)
    root = parsed["chunk"]["root_function"]
    registers = root["maximum_stack_size"]
    code = Code(root)
    for key, label, getter in callbacks:
        add_option_toggle(code, registers, key, label, getter, "Graphics", 1)
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
