"""Expose the two original lockpicking windows to the Android touch controls."""
from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path
from struct import iter_unpack, pack, unpack_from

from amalur_script import parse_bytes
from fit_veil_inventory import insert_callback, mark_window
from mark_veil_portraits import Code

# Window event handler, SHOW and HIDE positions in the released Veil scripts.
WINDOWS = {
    1714935: ("f359e9cf4e1787754210f0c0bfbaba87bf15f6b57aceb78b40e20619ca7c4265",
              9, "VeilLockSplash", 13, 54),
    1718924: ("7dd1a78c509e58a1e9cfcd9d8566001e4ebc8544a3fa1650dfbcc735e0891c74",
              22, "VeilLockGame", 31, 9),
}


def touch_pick_axis(fn):
    # The last menu tap can leave the guest in its mouse layout. While touch
    # owns the lock, read the virtual right stick even in that layout.
    r = fn["maximum_stack_size"]
    code = Code(fn)
    code.glob(r, "GAME")
    code.field(r, r, "get_personal_ini_entry_num")
    code.load(r + 1, "VeilLockTouch")
    code.load(r + 2, 0)
    code.op("CALL_I", r, 3, 2)
    code.op("EQ", 0, r, code.index(1) + 256)
    code.jump("original")
    code.load(2, 0)  # Original code tests register 2 against the mouse layout ID.
    code.mark("original")
    return code, r + 3


def patch_batch(batch):
    count, = unpack_from("<I", batch)
    end = 4 + 8 * count
    offset = end
    headers, scripts, seen = [], [], set()
    for key, size in iter_unpack("<II", batch[4:end]):
        script = batch[offset:offset + size]
        offset += size
        if key in WINDOWS:
            digest, child, marker, show, hide = WINDOWS[key]
            if key in seen or sha256(script).hexdigest() != digest:
                raise ValueError(f"Unrecognized lockpicking script {key}")
            seen.add(key)
            script = mark_window(script, child, marker, show, hide)
            if key == 1718924:
                script = insert_callback(script, 8, 27, touch_pick_axis)
            parse_bytes(script)
        headers.append(pack("<II", key, len(script)))
        scripts.append(script)
    if seen != WINDOWS.keys() or offset != len(batch):
        raise ValueError("Incomplete Veil script batch")
    return batch[:4] + b"".join(headers + scripts)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("batch", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(patch_batch(args.batch.read_bytes()))
