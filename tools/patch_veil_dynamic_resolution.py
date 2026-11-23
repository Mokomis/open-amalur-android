"""Add the Android dynamic resolution, heat and CPU performance switches to Veil's native Graphics menu."""
import argparse
from pathlib import Path
from struct import iter_unpack, pack, unpack_from

from amalur_script import parse_bytes
from fit_veil_inventory import insert_callback
from mark_veil_portraits import Code, constant_value, insert
from veil_choice_widgets import lookup
from veil_depth_choices import restart_footer
from patch_veil_environment import add_option_toggle, append_callbacks, repack_script


DYNAMIC = "VeilDynamicResolution"
HEAT = "VeilResolutionHeatResponse"
HINTS = "VeilCpuPerformanceHints"


def patch_released_toggle_template(data, root, keys):
    # The released menu already has the controller and touch pill selector.
    # Direct the new row to the same template branch as its parent.
    template = root["children"][112]
    constants = [constant_value(c) for c in template["constants"]]
    keys = [key for key in keys if key not in constants]
    if DYNAMIC not in constants or not keys:
        return data  # A fresh menu gets the entries from veil_gameplay_choices.
    ops = template["instructions"]
    site, = (op["pc"] for op in ops if op["opcode_name"] == "EQ"
             and op["C_raw"] == 256 + constants.index(DYNAMIC))
    if ops[site]["A"] != 1 or ops[site + 1]["opcode_name"] != "JMP":
        raise ValueError("Unrecognized Graphics toggle selector")

    def select_switch(fn):
        code = Code(fn)
        for i, key in enumerate(keys):
            code.op("EQ", 1, ops[site]["B_raw"], 256 + code.index(key))
            # Jump to the existing toggle branch, past the remaining comparisons and parent EQ.
            code.wide("JMP", 0, 65535 + 2 * (len(keys) - i - 1) + 1)
        return code, fn["maximum_stack_size"]
    return insert_callback(data, 112, site, select_switch)


def add_selection_help(fn):
    code = Code(fn)
    code.glob(0, "m_tab_listing")
    code.glob(1, "m_title_helper")
    code.field(1, 1, "m_cur_tab")
    lookup(code, 0, 0, 1)
    code.op("EQ", 0, 0, 256 + code.index("Graphics"))
    code.jump("done")
    code.glob(1, "m_list")
    code.op("SELF", 1, 1, 256 + code.index("get_selected"))
    code.op("CALL_I", 1, 2, 2)
    code.glob(2, "m_cur_options")
    lookup(code, 2, 2, 1)
    code.op("TEST", 2)
    code.jump("paint")
    code.field(2, 2, "id")
    code.mark("paint")
    restart_footer(code, selected_id=2)
    code.mark("done")
    return code, 17


def patch_options(data):
    parsed = parse_bytes(data)
    root = parsed["chunk"]["root_function"]
    constants = [constant_value(c) for c in root["constants"]]
    if HINTS in constants:
        raise ValueError("CPU performance hints are already installed")
    if not {"Graphics", "m_options_list", "m_veil_toggle_labels"}.issubset(
            value for value in constants if isinstance(value, str)):
        raise ValueError("Expected the Veil Graphics menu and environment options")
    if root["instructions"][-1]["opcode_name"] != "RETURN":
        raise ValueError("Unrecognized Veil options initialization")

    rows = []
    if DYNAMIC not in constants:
        rows.append((DYNAMIC, "Dynamic Resolution (60 FPS)"))
    if HEAT not in constants:
        rows.append((HEAT, "Reduce Resolution When Hot"))
    rows.append((HINTS, "CPU Performance Hints"))
    if DYNAMIC in constants:
        data = patch_released_toggle_template(data, root, [key for key, _ in rows])
    paint = root["children"][125]["instructions"]
    if paint[-1]["opcode_name"] != "RETURN":
        raise ValueError("Unrecognized Options refresh function")
    data = insert_callback(data, 125, paint[-1]["pc"], add_selection_help)
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
