"""Add the Android save import entry to the player's local Veil title menu."""

import argparse
from hashlib import sha256
from pathlib import Path
from struct import iter_unpack, pack, unpack_from
from zlib import crc32

from amalur_script import parse_bytes
from generate_veil_edits import index_of, read_entry
from mark_veil_name_dialog import BATCH_NAME
from mark_veil_portraits import Code, insert

MENU_SCRIPT = 13493
MENU_HASH = "8ea34afff60bdaa59a4afaf748f9b44e433ed70745f7fcdd2d0a3c7bbc3f167e"
IMPORT_ROW = 198030934


def import_action():
    """Encode an override that opens the Android chooser without entering a node."""
    code = Code({"constants": []})
    code.glob(1, "GAME")
    code.field(1, 1, "set_personal_ini_entry_str")
    code.load(2, "VeilImportSaves")
    code.load(3, "1")
    code.op("CALL_I", 1, 3, 1)
    code.op("RETURN", 0, 1)
    instructions = code.finish()
    # Two absent names, no source lines or upvalues, one node argument.
    header = pack(">6IBII", 0, 0, 0, 0, 0, 1, 0, 4, len(instructions) // 4)
    constants = pack(">I", len(code.values)) + code.new_constants
    # No children or debug arrays.
    return header + instructions + constants + pack(">4I", 0, 0, 0, 0)


def add_row(data):
    if sha256(data).hexdigest() != MENU_HASH:
        raise ValueError("Unrecognized Veil title menu script")
    parsed = parse_bytes(data)
    build = parsed["chunk"]["root_function"]["children"][17]
    child_count = len(build["children"])
    code = Code(build)
    code.field(2, 1, "children")
    code.field(3, 1, "children")
    code.op("LEN", 3, 3)
    code.op("ADD", 3, 3, 256 + code.index(1))
    code.op("NEWTABLE", 4, 0, 5)
    code.op("SETFIELD", 4, code.index("id_string"), 256 + code.index(IMPORT_ROW))
    code.op("SETFIELD", 4, code.index("title"), 256 + code.index("Import Saves"))
    # Button-tree overrides run before its submenu navigation.
    code.wide("CLOSURE", 5, child_count)
    code.op("SETFIELD", 4, code.index("override"), 5)
    for field in ("extra_wins", "children"):
        code.op("NEWTABLE", 5)
        code.op("SETFIELD", 4, code.index(field), 5)
    code.op("SETTABLE_S", 2, 3, 4)
    start, end, replacement = insert(data, build, 181, code, 6)
    debug = build["end_file_offset"] - 12
    data = (data[:start] + replacement + pack(">I", child_count + 1)
            + data[end + 4:debug] + import_action() + data[debug:])
    offset = parsed["wrapper"]["payload_file_offset"]
    data = data[:offset - 4] + pack("<I", len(data) - offset) + data[offset:]
    parse_bytes(data)
    return data


def patch_batch(batch):
    count, = unpack_from("<I", batch)
    offset = 4 + count * 8
    directory, records = [], []
    found = 0
    for identifier, size in iter_unpack("<II", batch[4:offset]):
        member = batch[offset:offset + size]
        offset += size
        if identifier == MENU_SCRIPT:
            member = add_row(member)
            found += 1
        directory.append(pack("<II", identifier, len(member)))
        records.append(member)
    if found != 1 or offset != len(batch):
        raise ValueError("Expected one title menu script in the Veil batch")
    return batch[:4] + b"".join(directory) + b"".join(records)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.archive.open("rb") as archive:
        batch = read_entry(archive, index_of(archive)[crc32(BATCH_NAME.encode("ascii"))])
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / BATCH_NAME).write_bytes(patch_batch(batch))
