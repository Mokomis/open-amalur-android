"""Add opt-in enemy tracking to the Android touch camera stick."""
import argparse
from pathlib import Path
from struct import iter_unpack, pack, unpack_from

from amalur_script import parse_bytes
from fit_veil_inventory import insert_callback
from mark_veil_portraits import Code, constant_value, insert
from patch_veil_dynamic_resolution import patch_released_toggle_template
from patch_veil_environment import add_option_toggle, append_callbacks, repack_script
from veil_choice_widgets import lookup

OPTION = "camera_touch_enemy_lock_on"
TARGET = "m_veil_touch_enemy"
DISTANCE = "veil_touch_enemy_distance"
UPDATE = "veil_update_touch_enemy"


def method(c, register, module, name):
    c.glob(register, module)
    c.field(register, register, name)


def call(c, register, module, name, args=(), results=1):
    method(c, register, module, name)
    for index, source in enumerate(args, register + 1):
        c.op("MOVE", index, source)
    c.op("CALL_I", register, len(args) + 1, results + 1)


def write_value(c, name, value, numeric=False):
    method(c, 10, "GAME", "set_personal_ini_entry_num" if numeric else "set_personal_ini_entry_str")
    c.load(11, name)
    c.op("MOVE", 12, value)
    c.op("CALL_I", 10, 3, 1)


def distance():
    c = Code({"constants": []})
    for predicate, reject_if_true in (("is_valid", False), ("is_player_enemy", False), ("is_dead", True)):
        call(c, 2, "ACTOR", predicate, (1,))
        c.op("TEST", 2, 0, int(reject_if_true))
        c.jump("invalid")
    call(c, 2, "ACTOR", "get_distance_2d", (0, 1))
    c.op("LE", 0, 2, 256 + c.index(3200))  # Retail Lockon's range.
    c.jump("invalid")
    c.op("RETURN", 2, 2)
    c.mark("invalid")
    c.op("LOADNIL", 2, 2)
    c.op("RETURN", 2, 2)
    return c


def update():
    c = Code({"constants": []})
    c.op("EQ", 1, 0, 256 + c.index(9))
    c.jump("release")
    c.glob(1, "get_player")
    c.op("CALL_I", 1, 1, 2)
    c.glob(3, TARGET)
    # Scripted, boss and vignette camera ownership takes priority.
    c.glob(4, "cameraLibrary")
    c.field(4, 4, "m_override_camera")
    c.op("TEST", 4, 0, 1)
    c.jump("unavailable")
    call(c, 4, "cameraLibrary", "is_camera_locked")
    c.op("TEST", 4, 0, 1)
    c.jump("unavailable")
    c.op("EQ", 0, 0, 256 + c.index(7))
    c.jump("track")
    c.op("TEST", 3, 0, 1)
    c.jump("release")
    method(c, 4, "GAME", "get_personal_ini_entry_num")
    c.load(5, OPTION)
    c.load(6, 0)
    c.op("CALL_I", 4, 3, 2)
    c.op("EQ", 0, 4, 256 + c.index(1))
    c.jump("end")

    # Prefer the enemy already targeted by the original attack controls.
    call(c, 3, "ACTOR", "get_target_object", (1,))

    def check(candidate):
        c.glob(7, DISTANCE)
        c.op("MOVE", 8, 1)
        c.op("MOVE", 9, candidate)
        c.op("CALL_I", 7, 3, 2)

    check(3)
    c.op("TEST", 7, 0, 1)
    c.jump("acquire")
    call(c, 4, "PLAYER", "get_npcs_targeting_player")
    c.op("TEST", 4)
    c.jump("end")
    c.load(5, 1)
    c.load(2, 3201)
    c.op("LEN", 6, 4)
    c.op("LOADNIL", 3, 3)
    c.mark("next")
    c.op("LE", 0, 5, 6)
    c.jump("chosen")
    lookup(c, 10, 4, 5)
    check(10)
    c.op("TEST", 7)
    c.jump("advance")
    c.op("LT", 0, 7, 2)
    c.jump("advance")
    c.op("MOVE", 2, 7)
    lookup(c, 3, 4, 5)
    c.mark("advance")
    c.op("ADD", 5, 5, 256 + c.index(1))
    c.jump("next")
    c.mark("chosen")
    c.op("TEST", 3)
    c.jump("end")
    c.mark("acquire")
    c.wide("SETGLOBAL", 3, c.index(TARGET))
    c.glob(4, "tostring")
    c.op("MOVE", 5, 3)
    c.op("CALL_I", 4, 2, 2)
    write_value(c, "VeilTouchTarget", 4)
    call(c, 4, "ACTOR", "set_target_object", (1, 3), results=0)
    c.jump("face")

    c.mark("track")
    c.op("TEST", 3)
    c.jump("release")
    check(3)
    c.op("TEST", 7)
    c.jump("release")
    # Use the same projection as the original enemy health bars. A missing
    # projection means the target is behind the camera.
    call(c, 4, "target_win_manager", "get_screen_loc", (3,))
    c.op("TEST", 4)
    c.jump("face")
    call(c, 5, "WINDOW", "get_screen_width")
    c.op("MUL", 4, 4, 256 + c.index(200))
    c.op("DIV", 4, 4, 5)
    c.op("SUB", 4, 4, 256 + c.index(100))
    write_value(c, "VeilTouchTargetX", 4, numeric=True)
    c.jump("end")

    c.mark("face")
    call(c, 4, "ACTOR", "get_angle_to_object", (1, 3))
    call(c, 5, "ACTOR", "set_angle", (1, 4), results=0)
    call(c, 4, "CAMERA", "recenter_boom_camera", results=0)
    c.load(4, 0)
    write_value(c, "VeilTouchTargetX", 4, numeric=True)
    c.jump("end")
    c.mark("unavailable")
    c.op("TEST", 3)
    c.jump("end")
    c.mark("release")
    c.op("LOADNIL", 3, 3)
    c.wide("SETGLOBAL", 3, c.index(TARGET))
    c.load(4, "0")
    write_value(c, "VeilTouchTarget", 4)
    c.mark("end")
    c.op("RETURN", 0, 1)
    return c


def root_of(data):
    return parse_bytes(data)["chunk"]["root_function"]


def install_at_end(data, code, stack):
    parsed = parse_bytes(data)
    root = parsed["chunk"]["root_function"]
    site = root["instructions"][-1]
    if site["opcode_name"] != "RETURN":
        raise ValueError("Unrecognized module initialization")
    a, b, part = insert(data, root, site["pc"], code, stack)
    return repack_script(data, parsed, data[:a] + part + data[b:])


def patch_hud(data):
    def dispatch(fn):
        c = Code(fn)
        # The native poll shared with the HUD returns a number for these events.
        c.op("EQ", 1, 2, 256 + c.index(0))
        c.jump("end")
        for event in (7, 8, 9):
            c.op("EQ", 1, 2, 256 + c.index(event))
            c.jump("dispatch")
        c.jump("end")
        c.mark("dispatch")
        c.glob(3, UPDATE)
        c.op("MOVE", 4, 2)
        c.op("CALL_I", 3, 2, 1)
        c.mark("end")
        return c, 5

    root = root_of(data)
    fn = root["children"][14]
    site, = (op["pc"] + 3 for op in fn["instructions"] if op["opcode_name"] == "LOADK"
             and constant_value(fn["constants"][op["Bx_raw"]]) == "VeilTabletMenu")
    if fn["instructions"][site-1]["opcode_name"] != "CALL_I":
        raise ValueError("Unrecognized gameplay input poll")
    data = insert_callback(data, 14, site, dispatch)
    parsed = parse_bytes(data)
    children = parsed["chunk"]["root_function"]["children"]
    a, b = children[0]["file_offset"] - 4, children[-1]["end_file_offset"]
    additions = []
    functions = [(DISTANCE, distance(), 2, 5), (UPDATE, update(), 1, 13)]
    for _, c, args, stack in functions:
        words = c.finish()
        additions.append(pack(">6IBII", 0, 0, 0, 0, 0, args, 0, stack, len(words) // 4)
                         + words + pack(">I", len(c.values)) + c.new_constants + bytes(16))
    data = repack_script(data, parsed, data[:a] + pack(">I", len(children) + len(additions))
                         + data[a+4:b] + b"".join(additions) + data[b:])
    c = Code(root_of(data))
    for i, (name, _, _, _) in enumerate(functions, len(children)):
        c.wide("CLOSURE", 0, i)
        c.wide("SETGLOBAL", 0, c.index(name))
    return install_at_end(data, c, 1)


def patch_options(data):
    data = patch_released_toggle_template(data, root_of(data), [OPTION])
    # Reuse the existing selection footer, including its clearing and repaint.
    fn = root_of(data)["children"][125]
    ops = fn["instructions"]
    values = [constant_value(v) for v in fn["constants"]]
    sites = [op["pc"] for op in ops if op["opcode_name"] == "EQ" and op["B_raw"] == 0
             and op["C_raw"] == 256 + values.index("Graphics")]
    footer = next(op["pc"] for op in reversed(ops) if op["opcode_name"] == "CALL_I")
    guard, _ = sites[-2:]

    def camera_help(fn):
        c = Code(fn)
        c.op("EQ", 0, 0, 256 + c.index("Camera"))
        c.jump("end")
        c.op("EQ", 0, 2, 256 + c.index(OPTION))
        c.jump("end")
        c.load(14, "Hold the right touch stick to lock onto an enemy.\nHold again to release.")
        c.mark("end")
        return c, 17

    def include_camera(fn):
        c = Code(fn)
        c.op("EQ", 1, 0, 256 + c.index("Camera"))
        c.wide("JMP", 0, 65535 + 2)  # Join Graphics at its selected-row lookup.
        return c, 1

    data = insert_callback(data, 125, footer, camera_help)
    data = insert_callback(data, 125, guard, include_camera)
    data, getter = append_callbacks(data, OPTION)
    root = root_of(data)
    r = root["maximum_stack_size"]
    c = Code(root)
    add_option_toggle(c, r, OPTION, "Touch Enemy Lock-On", getter, "Camera", 1)
    return install_at_end(data, c, r + 6)


def patch_batch(data):
    edits = {13523: patch_options, 1402689: patch_hud}
    pos = 4 + 8 * unpack_from("<I", data)[0]
    headers, scripts = [], []
    for key, size in iter_unpack("<II", data[4:pos]):
        script = data[pos:pos + size]
        pos += size
        if key in edits:
            if OPTION.encode() in script:
                raise ValueError("Touch Enemy Lock-On already installed")
            script = edits.pop(key)(script)
            parse_bytes(script)
        scripts.append(script)
        headers.append(pack("<II", key, len(script)))
    if edits or pos != len(data):
        raise ValueError("Missing Android touch camera scripts")
    return data[:4] + b"".join(headers + scripts)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("batch", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(patch_batch(args.batch.read_bytes()))
