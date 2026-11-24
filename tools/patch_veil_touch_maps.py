"""Expose touch waypoint and pinch commands in the original local/world map."""

from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path
from struct import iter_unpack, pack, unpack_from

from amalur_script import parse_bytes
from fit_veil_inventory import insert_callback
from mark_veil_portraits import Code
from patch_veil_touch_target import method

MAP_SCRIPT = 13519
MAP_HASH = "eee6f09e1b9b3443ce810f54157e0b25ba5055c74a148a9fd08eadcda2aa1c37"
TOUCH_CURSOR = "m_veil_touch_cursor"
MOUSE_CONFIG = 283690  # The game's own mouse layout enum.


def native_setting(c, r, name):
    method(c, r, "GAME", "get_personal_ini_entry_num")
    c.load(r + 1, name)
    c.load(r + 2, 0)
    c.op("CALL_I", r, 3, 2)


def touch_mouse_config(register):
    def insert(fn):
        c = Code(fn)
        free = fn["maximum_stack_size"]
        c.glob(free, TOUCH_CURSOR)
        c.op("TEST", free)
        c.jump("done")
        c.load(register, MOUSE_CONFIG)
        c.mark("done")
        return c, free + 1
    return insert


def suppress_drag_click(fn):
    # First instruction inside the world map's original LEFT_DOWN branch.
    c = Code(fn)
    r = fn["maximum_stack_size"]
    native_setting(c, r, "VeilMapDrag")
    c.op("EQ", 0, r, 256 + c.index(1))
    c.jump("done")
    c.op("RETURN", 0, 1)
    c.mark("done")
    return c, r + 3


def map_commands(fn):
    c = Code(fn)
    command = fn["maximum_stack_size"]
    r = command + 1

    native_setting(c, command, "VeilMapGesture")
    c.op("EQ", 1, command, 256 + c.index(0))
    c.jump("done")

    # Consume even if a menu is blocking the map. Do not replay an old gesture
    # after a travel confirmation, context menu, or modal tutorial is closed.
    for modal in ("message_box_win", "m_right_click_menu"):
        c.native(r, "is_visible")
        c.glob(r + 1, modal)
        c.field(r + 1, r + 1, "m_window")
        c.op("CALL_I", r, 2, 2)
        c.op("TEST", r, 0, 1)
        c.jump("done")
    method(c, r, "tutorial_win", "is_modal_tutorial_active")
    c.op("CALL_I", r, 1, 2)
    c.op("TEST", r, 0, 1)
    c.jump("done")
    # Both maps share this script. Only the actual map surface accepts input
    # (the Back button, map switcher, and footer are siblings).
    for window in ("m_world_map", "m_region_map"):
        c.native(r, "is_moused_over_window_a_child_of")
        c.glob(r + 1, window)
        c.op("CALL_I", r, 2, 2)
        c.op("TEST", r, 0, 1)
        c.jump("map_surface")
    c.jump("done")

    c.mark("map_surface")
    c.op("EQ", 1, command, 256 + c.index(17))
    c.jump("waypoint")
    # Signed pinch steps encoded as 1..8 in and 9..16 out. Use the retail
    # zoom increment, scaling curves, bounds, and mode-specific redraw.
    c.op("LE", 1, command, 256 + c.index(8))
    c.jump("zoom")
    c.load(r, 8)
    c.op("SUB", command, r, command)
    c.mark("zoom")
    c.glob(r, "zoom")
    c.glob(r + 1, "m_ZOOM_INCREMENT")
    c.op("MUL", r + 1, command, r + 1)
    c.op("CALL_I", r, 2, 1)
    c.jump("done")

    c.mark("waypoint")
    # get_map_windows_under_target and get_target_location_in_window normally
    # center the cursor when a pad is connected. Use the actual finger for
    # just this call, including the world map's per-region coordinate tests.
    c.op("LOADBOOL", r, 1)
    c.wide("SETGLOBAL", r, c.index(TOUCH_CURSOR))
    c.glob(r, "move_cursor")
    c.op("CALL_I", r, 1, 1)
    c.op("LOADNIL", r, r)
    c.wide("SETGLOBAL", r, c.index(TOUCH_CURSOR))
    c.glob(r, "get_current_mapwin")
    c.op("CALL_I", r, 1, 2)
    c.op("TEST", r)
    c.jump("done")
    method(c, r + 1, "MAP", "is_waypoint_at_cursor")
    c.op("MOVE", r + 2, r)
    c.op("CALL_I", r + 1, 2, 2)
    c.op("TEST", r + 1, 0, 1)
    c.jump("remove")
    c.glob(r + 1, "m_last_target_sprite")
    c.op("TEST", r + 1)
    c.jump("done")
    c.glob(r + 1, "place_waypoint")
    c.op("CALL_I", r + 1, 1, 1)
    c.jump("refresh")
    c.mark("remove")
    c.glob(r + 1, "unplace_waypoint")
    c.op("CALL_I", r + 1, 1, 1)
    c.mark("refresh")
    c.glob(r, "update_rose")
    c.op("CALL_I", r, 1, 1)
    c.mark("done")
    return c, r + 3


def patch_batch(data):
    count, = unpack_from("<I", data)
    pos = 4 + count * 8
    headers, scripts = [], []
    found = False
    for key, size in iter_unpack("<II", data[4:pos]):
        script = data[pos:pos + size]
        pos += size
        if key == MAP_SCRIPT:
            if found or sha256(script).hexdigest() != MAP_HASH:
                raise ValueError("Unrecognized local/world map window")
            found = True
            # The three sites immediately follow explicit get_ui_config calls.
            for child, pc, reg in ((8, 27, 4), (45, 37, 3), (45, 11, 3)):
                script = insert_callback(script, child, pc, touch_mouse_config(reg))
            script = insert_callback(script, 36, 39, map_commands)
            script = insert_callback(script, 54, 7, suppress_drag_click)
            parse_bytes(script)
        headers.append(pack("<II", key, len(script)))
        scripts.append(script)
    if not found or pos != len(data):
        raise ValueError("Incomplete map script batch")
    return data[:4] + b"".join(headers + scripts)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("batch", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(patch_batch(args.batch.read_bytes()))
