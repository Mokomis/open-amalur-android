"""Add Veil's environment options and forward the original game's outdoor clock."""

import argparse
from hashlib import sha256
from pathlib import Path
from struct import iter_unpack, pack, unpack_from

from amalur_script import parse_bytes
from fit_veil_inventory import insert_callback
from mark_veil_portraits import Code, constant_value, insert

TOGGLES = (
    ("VeilNightAmbience", "Night Ambience"),
    ("VeilDayAmbience", "Day Ambience"),
)


def repack_script(data, parsed, replacement):
    offset = parsed["wrapper"]["payload_file_offset"]
    return replacement[:offset - 4] + pack("<I", len(replacement) - offset) + replacement[offset:]


def append_callbacks(data, key):
    parsed = parse_bytes(data)
    children = parsed["chunk"]["root_function"]["children"]
    first = children[0]["file_offset"] - 4
    end = children[-1]["end_file_offset"]
    def constant(text):
        raw = text.encode("ascii") + b"\0"
        return pack(">I", len(raw)) + raw

    callbacks = []
    for child in children[183:185]:
        original = data[child["file_offset"]:child["end_file_offset"]]
        if original.count(constant("VeilRenderResolution")) != 1:
            raise ValueError("Unrecognized options callback")
        callback = original.replace(constant("VeilRenderResolution"), constant(key))
        callbacks.append(callback.replace(constant("m_veil_resolution_labels"), constant("m_veil_toggle_labels")))
    result = data[:first] + pack(">I", len(children) + len(callbacks)) + data[first + 4:end]
    result += b"".join(callbacks) + data[end:]
    return repack_script(data, parsed, result), len(children)


def add_audio_toggle(code, register, key, label, get):
    call, section, row, value, field, second = range(register, register + 6)

    def add_field(name, literal=None):
        if literal is not None:
            code.load(value, literal)
        code.load(field, name)
        code.op("SETTABLE_S", row, field, value)

    def add_array(name, first, last):
        code.op("NEWTABLE", value, 2, 0)
        code.load(field, first)
        code.load(second, last)
        code.op("SETLIST", value, 2, 1)
        add_field(name)

    code.glob(call, "table")
    code.load(row, "insert")
    code.op("GETTABLE_S", call, call, row)
    code.op("DATA")
    code.op("DATA")
    code.glob(section, "m_options_list")
    code.field(section, section, "Audio")
    code.op("NEWTABLE", row, 0, 7)
    add_field("id", key)
    add_field("display", label)
    add_field("default", 2)
    code.wide("CLOSURE", value, get)
    add_field("get")
    code.wide("CLOSURE", value, get + 1)
    add_field("callback")
    add_array("params", 0, 1)
    add_array("display_vals", "Off", "On")
    code.op("NEWTABLE", value, 0, 2)
    code.op("LOADBOOL", field, 1, 0)
    for menu in ("main", "ledger"):
        code.load(second, menu)
        code.op("SETTABLE_S", value, second, field)
    add_field("menu_type")
    code.op("CALL_I", call, 3, 1)


def patch_options(data):
    if sha256(data).hexdigest() != "3fa5cad9d1337b3a98f140221803026ac29a7a2f8d6cb83da3e271453374a861":
        raise ValueError("Unrecognized Veil options")
    callbacks = []
    for key, label in TOGGLES:
        data, get = append_callbacks(data, key)
        callbacks.append((key, label, get))
    parsed = parse_bytes(data)
    fn = parsed["chunk"]["root_function"]
    base = fn["maximum_stack_size"]
    code = Code(fn)
    code.op("NEWTABLE", base, 2, 0)
    code.load(base + 1, "Off")
    code.load(base + 2, "On")
    code.op("SETLIST", base, 2, 1)
    code.wide("SETGLOBAL", base, code.index("m_veil_toggle_labels"))
    for row in callbacks:
        add_audio_toggle(code, base, *row)
    start, end, replacement = insert(data, fn, 2914, code, base + 6)
    return repack_script(data, parsed, data[:start] + replacement + data[end:])


def patch_hud(data):
    def add_clock(fn):
        call, key, value, minute, temp = range(fn["maximum_stack_size"], fn["maximum_stack_size"] + 5)
        code = Code(fn)

        def method(register, module, name):
            code.glob(register, module)
            code.field(register, register, name)

        method(call, "GAME", "get_personal_ini_entry_num")
        code.load(key, "VeilEnvironmentPoll")
        code.load(value, 0)
        code.op("CALL_I", call, 3, 2)
        code.op("EQ", 1, call, code.index(1) + 256)
        code.jump("sample")
        code.op("EQ", 0, call, code.index("1") + 256)
        code.jump("done")

        code.mark("sample")
        method(call, "map_data", "is_world_map_region")
        method(key, "ACTOR", "get_actor_region")
        code.glob(value, "get_player")
        code.op("CALL_I", value, 1, 2)
        code.op("CALL_I", key, 2, 2)
        code.op("CALL_I", call, 2, 2)
        code.op("TEST", call, 0, 0)
        code.jump("indoors")

        method(minute, "GAME", "get_current_hour_in_day")
        code.op("CALL_I", minute, 1, 2)
        code.op("MUL", minute, minute, code.index(60) + 256)
        method(temp, "GAME", "get_current_minute_in_hour")
        code.op("CALL_I", temp, 1, 2)
        code.op("ADD", minute, minute, temp)
        code.op("ADD", minute, minute, code.index(1) + 256)
        code.op("MUL", minute, minute, code.index(101) + 256)
        method(temp, "PROFILE", "get_sound_fx_volume")
        code.op("CALL_I", temp, 1, 2)
        code.op("ADD", value, minute, temp)
        code.jump("send")

        code.mark("indoors")
        code.load(value, 0)
        code.mark("send")
        method(call, "GAME", "set_personal_ini_entry_num")
        code.load(key, "VeilOutdoorClock")
        code.op("CALL_I", call, 3, 1)
        code.mark("done")
        return code, temp + 1

    parsed = parse_bytes(data)
    fn = parsed["chunk"]["root_function"]["children"][14]
    marker = fn["instructions"][41]
    if marker["opcode_name"] != "LOADK" or constant_value(fn["constants"][marker["Bx_raw"]]) != "VeilTabletHud":
        raise ValueError("Unrecognized Veil gameplay HUD")
    return insert_callback(data, 14, 44, add_clock)


def patch_batch(data):
    count, = unpack_from("<I", data)
    offset = 4 + count * 8
    callbacks = {13523: patch_options, 1402689: patch_hud}
    directory, scripts = [], []
    for key, length in iter_unpack("<II", data[4:offset]):
        script = data[offset:offset + length]
        offset += length
        if key in callbacks:
            script = callbacks.pop(key)(script)
            parse_bytes(script)
        directory.append(pack("<II", key, len(script)))
        scripts.append(script)
    if callbacks or offset != len(data):
        raise ValueError("Missing scripts for the Veil environment")
    return data[:4] + b"".join(directory) + b"".join(scripts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("batch", type=Path)
    args = parser.parse_args()
    args.batch.write_bytes(patch_batch(args.batch.read_bytes()))


if __name__ == "__main__":
    main()
