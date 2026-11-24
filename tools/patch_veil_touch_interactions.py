"""Apply the Android touch interaction fixes to the released 0.1.33 batch."""
from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path
from struct import iter_unpack, pack, unpack_from

from amalur_script import parse_bytes
from fit_veil_inventory import insert_callback
from mark_veil_portraits import Code
from patch_veil_touch_maps import patch_batch as patch_maps
from patch_veil_touch_sliders import patch_batch as patch_sliders
from patch_veil_touch_target import method


def touch_aggression(fn):
    # The original update already tested WINDOW.is_visible. The skull, health
    # and mana alerts share this window. Only the active skull offers D-pad Up.
    c = Code(fn)
    r = fn["maximum_stack_size"]
    c.glob(r, "m_cur_type")
    c.op("EQ", 0, r, 256 + c.index("aggressive"))
    c.jump("done")
    method(c, r, "Passive", "get_disable_aggressive_mode")
    c.op("CALL_I", r, 1, 2)
    c.op("TEST", r, 0, 1)
    c.jump("done")
    method(c, r, "TIME", "get_remaining_time")
    c.glob(r + 1, "m_AGGRESSIVE_ALERT_TIMER")
    c.op("CALL_I", r, 2, 2)
    c.op("LT_BK", 0, c.index(0), r)
    c.jump("done")
    method(c, r, "GAME", "get_personal_ini_entry_num")
    c.load(r + 1, "VeilAggressionTouch")
    c.load(r + 2, 0)
    c.op("CALL_I", r, 3, 2)
    c.op("EQ", 1, r, 256 + c.index(1))
    c.jump("tap")
    c.op("EQ", 0, r, 256 + c.index("1"))
    c.jump("done")
    c.mark("tap")
    c.glob(r, "on_inputdeviceaction_event")
    c.op("LOADNIL", r + 1, r + 1)
    c.load(r + 2, "dpad_up")
    c.op("CALL_I", r, 3, 1)
    c.mark("done")
    return c, r + 3


def patch_aggression(data):
    count, = unpack_from("<I", data)
    offset = 4 + 8 * count
    headers, members = [], []
    found = False
    for key, size in iter_unpack("<II", data[4:offset]):
        script = data[offset:offset + size]
        offset += size
        if key == 1475820:
            if found or sha256(script).hexdigest() != "9b19524a1b079c7d89c62d90e8fdd175899b9a8872ab2f7a99be6220b8f22ae0":
                raise ValueError("Unknown aggressive-mode HUD layout")
            script = insert_callback(script, 11, 45, touch_aggression)
            parse_bytes(script)
            found = True
        headers.append(pack("<II", key, len(script)))
        members.append(script)
    if not found or offset != len(data):
        raise ValueError("Incomplete interaction script batch")
    return data[:4] + b"".join(headers + members)


def patch_batch(data):
    return patch_maps(patch_sliders(patch_aggression(data)))


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("batch", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(patch_batch(args.batch.read_bytes()))
