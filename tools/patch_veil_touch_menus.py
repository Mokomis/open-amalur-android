"""Expose merchant footer actions and activate tapped rows in Load Game."""

import argparse
from hashlib import sha256
from pathlib import Path
from struct import iter_unpack, pack, unpack_from

from amalur_script import parse_bytes
from fit_veil_inventory import insert_callback, show_footer_buttons
from mark_veil_portraits import Code

SAVE_RESTORE = 697815
MERCHANT = 24541
MERCHANT_HASH = "c2bd007d4b9ed699f3b6ef4703844070068d046c99e665d4bd1b713ebc5b9fe3"
SAVE_HASH = "4f02f0666e764a44dbc00a6baa9032d6b47a8c309b382236756d70813041cbb4"


def load_clicked_row(fn):
    # This insertion follows the original CLICKED and primary-button checks.
    # A different handler owns the X/delete control.
    code = Code(fn)
    code.glob(4, "m_curr_mode")
    code.glob(5, "LOAD")
    code.op("EQ", 0, 4, 5)
    code.jump("other_mode")

    code.glob(4, "m_list")
    code.op("SELF", 4, 4, 256 + code.index("get_button_id"))
    code.op("MOVE", 6, 1)
    code.op("CALL_I", 4, 3, 2)
    code.op("TEST", 4)
    code.jump("done")
    code.glob(5, "set_selected_slot")
    code.op("MOVE", 6, 4)
    code.op("CALL_I", 5, 2, 1)
    code.glob(5, "provide_update")
    code.op("CALL_I", 5, 1, 1)

    # The controller's confirm path checks slot readiness, DLC, and load state.
    code.glob(5, "handle_input")
    code.op("NEWTABLE", 6, 0, 1)
    code.load(7, "a")
    code.op("SETFIELD", 6, code.index("info"), 7)
    code.op("CALL_I", 5, 2, 1)
    code.mark("done")
    code.op("RETURN", 0, 1)
    code.mark("other_mode")
    return code, 8


def patch_batch(batch):
    count, = unpack_from("<I", batch)
    offset = 4 + count * 8
    headers, scripts = [], []
    seen = set()
    for key, size in iter_unpack("<II", batch[4:offset]):
        script = batch[offset:offset + size]
        offset += size
        if key == SAVE_RESTORE:
            if key in seen or sha256(script).hexdigest() != SAVE_HASH:
                raise ValueError("Unrecognized save/restore window")
            seen.add(key)
            script = insert_callback(script, 23, 20, load_clicked_row)
        elif key == MERCHANT:
            if key in seen or sha256(script).hexdigest() != MERCHANT_HASH:
                raise ValueError("Unrecognized merchant window")
            seen.add(key)
            # Use the original clickable footer for Buy, Sell, Inspect/Compare,
            # and category expansion. The game still disables ineligible actions.
            script = show_footer_buttons(script, 18, (56, 71, 113, 147, 162, 185, 242))
        if key in (SAVE_RESTORE, MERCHANT):
            parse_bytes(script)
        headers.append(pack("<II", key, len(script)))
        scripts.append(script)
    if seen != {SAVE_RESTORE, MERCHANT} or offset != len(batch):
        raise ValueError("Incomplete menu script batch")
    return batch[:4] + b"".join(headers + scripts)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("batch", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(patch_batch(args.batch.read_bytes()))
