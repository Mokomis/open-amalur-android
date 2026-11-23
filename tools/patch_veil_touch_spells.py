"""Map a learned spell to a numbered slot by touch and use the game's Quick Save."""
from pathlib import Path
from struct import iter_unpack, pack, unpack_from
from hashlib import sha256
import argparse

from amalur_script import parse_bytes
from fit_veil_inventory import insert_callback
from mark_veil_portraits import Code, constant_value
from patch_veil_touch_target import method

PENDING = "m_veil_spell_to_map"


def clear_pending(fn):
    c = Code(fn)
    r = fn["maximum_stack_size"]
    c.op("LOADNIL", r, r)
    c.wide("SETGLOBAL", r, c.index(PENDING))
    return c, r + 1


def select_touched(fn):
    c, stack = clear_pending(fn)
    r = stack - 1
    # LEFT_DOWN supplies the button ID before the next hover/update tick.
    c.glob(r, "set_selected_slot")
    c.op("MOVE", r + 1, 10)
    c.op("CALL_I", r, 2, 1)
    return c, r + 2


def remember_spell(fn):
    c = Code(fn)
    r = fn["maximum_stack_size"]
    method(c, r, "GAME", "get_personal_ini_entry_num")
    c.load(r + 1, "VeilTabletInput")
    c.load(r + 2, 0)
    c.op("CALL_I", r, 3, 2)
    c.op("EQ", 0, r, 256 + c.index(1))
    c.jump("end")
    # The original ownership, active-ability, tutorial and level-up guards passed.
    c.wide("SETGLOBAL", 11, c.index(PENDING))
    c.mark("end")
    return c, r + 3


def assignment_hint(fn):
    c = Code(fn)
    r = fn["maximum_stack_size"]
    c.glob(r, PENDING)
    c.op("EQ", 0, r, 2)  # R2 is the ability whose description is being built.
    c.jump("end")
    c.native(r, "add_to_build_string")
    c.load(r + 1, "Tap a numbered slot below to assign this ability.\n\n")
    c.op("CALL_I", r, 2, 1)
    c.mark("end")
    return c, r + 2


def assign_to_slot(fn):
    c = Code(fn)
    r = fn["maximum_stack_size"]
    c.glob(r, "abilities_win")
    c.field(r + 1, r, PENDING)
    c.op("TEST", r + 1)
    c.jump("end")
    c.op("TEST", 6)  # Mouse hit resolved by the bar to a real slot.
    c.jump("end")
    c.field(r + 2, r, "m_curr_mode")
    c.op("EQ", 0, r + 2, 256 + c.index("abilities_menu"))
    c.jump("end")
    c.native(r + 2, "is_visible")
    c.field(r + 3, r, "m_window")
    c.op("CALL_I", r + 2, 2, 2)
    c.op("TEST", r + 2)
    c.jump("end")
    method(c, r + 2, "GAME", "get_personal_ini_entry_num")
    c.load(r + 3, "VeilTabletInput")
    c.load(r + 4, 0)
    c.op("CALL_I", r + 2, 3, 2)
    c.op("EQ", 0, r + 2, 256 + c.index(1))
    c.jump("end")
    c.glob(r + 2, "handle_drop")
    c.op("MOVE", r + 3, 6)
    c.op("MOVE", r + 4, r + 1)
    c.op("CALL_I", r + 2, 3, 1)
    c.op("RETURN", 0, 1)
    c.mark("end")
    return c, r + 5


def finish_drop(fn):
    c = Code(fn)
    r = fn["maximum_stack_size"]
    c.glob(r, "abilities_win")
    c.op("LOADNIL", r + 1, r + 1)
    c.op("SETFIELD", r, c.index(PENDING), r + 1)
    return c, r + 2


def quick_save(fn):
    c = Code(fn)
    c.op("EQ", 0, 2, 256 + c.index(10))
    c.jump("end")
    method(c, 3, "save_restore_win", "quick_save")
    c.op("CALL_I", 3, 1, 1)
    c.mark("end")
    return c, 4


def publish_hud_mode(fn, mode, on_update=False):
    c = Code(fn)
    r = fn["maximum_stack_size"]
    if on_update:
        # The HUD has already read the current profile mode into this register.
        # Report it on the first gameplay frame and when it changes.
        key = "m_veil_hotbar_hud_mode"
        c.glob(r, key)
        c.op("EQ", 1, r, mode)
        c.jump("end")
        c.wide("SETGLOBAL", mode, c.index(key))
    method(c, r, "GAME", "set_personal_ini_entry_num")
    c.load(r + 1, "VeilHotbarHudMode")
    c.op("MOVE", r + 2, mode)
    c.op("CALL_I", r, 3, 1)
    c.mark("end")
    return c, r + 3


def patch_batch(data):
    patches = {
        # The tree's valid drag source also works as the source for a two-tap assignment.
        13511: [(45, 147, remember_spell), (45, 109, select_touched),
                (10, 192, assignment_hint), (16, 0, clear_pending),
                (22, 0, clear_pending), (25, 10, clear_pending)],
        1366961: [(19, 24, assign_to_slot), (14, 4, finish_drop)],
        # Publish the option immediately even when Hidden stops the HUD updating.
        13523: [(9, 36, lambda fn: publish_hud_mode(fn, 3))],
        1402689: [(9, 13, lambda fn: publish_hud_mode(fn, 1, on_update=True))],
    }
    pos = 4 + 8 * unpack_from("<I", data)[0]
    headers, scripts = [], []
    for key, size in iter_unpack("<II", data[4:pos]):
        script = data[pos:pos + size]
        pos += size
        if key in patches:
            changes = patches.pop(key)
            if sha256(script).hexdigest() != HASHES[key]:
                raise ValueError(f"Unrecognized Veil touch script {key}")
            if key == 1402689:
                fn = parse_bytes(script)["chunk"]["root_function"]["children"][14]
                constants = [constant_value(v) for v in fn["constants"]]
                site, = (op["pc"] for op in fn["instructions"] if op["opcode_name"] == "LOADK" and
                         constants[op["Bx_raw"]] == "VeilTabletMenu")
                changes.append((14, site + 3, quick_save))
            for child, site, writer in changes:
                script = insert_callback(script, child, site, writer)
            parse_bytes(script)
        scripts.append(script)
        headers.append(pack("<II", key, len(script)))
    if patches or pos != len(data):
        raise ValueError("Incomplete ability menu or HUD")
    return data[:4] + b"".join(headers + scripts)


HASHES = {
    13511: "40417d8ce700b516cc1cb2c164a05f5a0563a95e5e2fd5e49725d714d82c5abc",
    13523: "b7df3cc6f9f9de6b9d0feb0e73714aad4e03bfb80984d454220313776946e040",
    1366961: "73f41e494e2559289832917c7dbb4a9028a163da080f1187a61addc1766d7a6f",
    1402689: "09773e58cd48bda58dea432757eb15d9241f07a49803034962a89f9a57a2a0b0",
}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("batch", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(patch_batch(args.batch.read_bytes()))
