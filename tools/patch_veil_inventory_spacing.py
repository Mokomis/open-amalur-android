"""Position the item card beside the inventory list at the current UI scale."""

import argparse
from hashlib import sha256
from pathlib import Path
from struct import iter_unpack, pack, unpack_from

from amalur_script import parse_bytes
from fit_veil_inventory import insert_callback
from mark_veil_portraits import Code, constant_value

INVENTORY = 13517
SOURCE_HASH = "e17daa62a71ab970b7c83d2fb247394b69218b4588566f1dd51ed4a6534ae10d"


def keep_list_clear(fn, x):
    # The next guest instruction moves the card with its existing X and Y.
    # These window metrics and the template position are in the same UI units.
    code = Code(fn)
    start = fn["maximum_stack_size"]
    edge, width = start, start + 1
    code.native(edge, "get_left")
    code.glob(edge + 1, "m_list_wrapper")
    code.op("CALL_I", edge, 2, 2)
    code.native(width, "get_width")
    code.glob(width + 1, "m_list_wrapper")
    code.op("CALL_I", width, 2, 2)
    code.op("ADD", edge, edge, width)
    # The wrapper sits inside the wider pocket background. Leave room for its
    # trailing inset as well as the gap before the item card.
    code.op("DIV", width, width, 256 + code.index(5))
    code.op("ADD", edge, edge, width)
    code.op("LT", 0, x, edge)
    code.jump("ready")
    code.op("MOVE", x, edge)
    code.mark("ready")
    return code, start + 3


def patch_inventory(script):
    if sha256(script).hexdigest() != SOURCE_HASH:
        raise ValueError("Unrecognized inventory positioning script")
    functions = parse_bytes(script)["chunk"]["root_function"]["children"]
    # Pocket display, entry, and both ways of highlighting a weapon slot.
    sites = ((48, 82, 3), (61, 581, 9), (61, 555, 9), (61, 399, 5))
    output = bytearray(script)
    for child, pc, x in sites:
        fn = functions[child]
        op = fn["instructions"][pc]
        if (op["opcode_name"] != "SUB" or op["A"] != x or op["B_raw"] != x or
                constant_value(fn["constants"][op["C_raw"] - 256]) != 74):
            raise ValueError("Unexpected fixed item-card offset")
        # Recover the original template X before applying the list margin.
        code = Code(fn)
        code.op("MOVE", x, x)
        offset = op["file_offset"]
        output[offset:offset + 4] = code.finish()
    result = bytes(output)
    for child, pc, x in sites:
        result = insert_callback(result, child, pc + 1, lambda fn: keep_list_clear(fn, x))
    parse_bytes(result)
    return result


def patch_batch(batch):
    count, = unpack_from("<I", batch)
    offset = 4 + count * 8
    headers, scripts = [], []
    seen = 0
    for key, size in iter_unpack("<II", batch[4:offset]):
        script = batch[offset:offset + size]
        offset += size
        if key == INVENTORY:
            script = patch_inventory(script)
            seen += 1
        headers.append(pack("<II", key, len(script)))
        scripts.append(script)
    if seen != 1 or offset != len(batch):
        raise ValueError("Incomplete inventory script batch")
    return batch[:4] + b"".join(headers + scripts)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("batch", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(patch_batch(args.batch.read_bytes()))
