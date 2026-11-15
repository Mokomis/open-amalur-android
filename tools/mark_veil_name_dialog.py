"""Route touch to the original name dialog while it owns modal input."""

import argparse
from hashlib import sha256
from pathlib import Path
from struct import iter_unpack, pack, unpack_from
from zlib import crc32

from amalur_script import parse_bytes
from generate_veil_edits import index_of, read_entry
from mark_veil_portraits import Code, insert

BATCH_NAME = "134230570_klua.batch"
NAME_SCRIPT = 2031255
NAME_SHA256 = "2464178b619847eb79a95ba39a5d45d82f1659532eb49c71f1adc7884b39b4e1"


def mark_dialog(data):
    if sha256(data).hexdigest() != NAME_SHA256:
        raise ValueError("Unrecognized original name dialog script")
    parsed = parse_bytes(data)
    functions = parsed["chunk"]["root_function"]["children"]
    # START_CLOSE clears input ownership before the original callback finishes.
    # Init claims it only after the original window has established modal focus.
    for function, pc, active in ((functions[4], 16, "0"), (functions[1], 142, "1")):
        register = function["maximum_stack_size"]
        code = Code(function)
        code.glob(register, "GAME")
        code.field(register, register, "set_personal_ini_entry_str")
        code.load(register + 1, "VeilNameDialog")
        code.load(register + 2, active)
        code.op("CALL_I", register, 3, 1)
        start, end, replacement = insert(data, function, pc, code, register + 3)
        data = data[:start] + replacement + data[end:]
    offset = parsed["wrapper"]["payload_file_offset"]
    data = data[:offset - 4] + pack("<I", len(data) - offset) + data[offset:]
    parse_bytes(data)
    return data


def patch_batch(batch):
    count, = unpack_from("<I", batch)
    offset = 4 + count * 8
    directory, data = [], []
    matched = 0
    for identifier, size in iter_unpack("<II", batch[4:offset]):
        member = batch[offset:offset + size]
        offset += size
        if identifier == NAME_SCRIPT:
            member = mark_dialog(member)
            matched += 1
        directory.append(pack("<II", identifier, len(member)))
        data.append(member)
    if matched != 1 or offset != len(batch):
        raise ValueError("Expected one name dialog in the Veil UI batch")
    return batch[:4] + b"".join(directory) + b"".join(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.archive.open("rb") as pak:
        batch = read_entry(pak, index_of(pak)[crc32(BATCH_NAME.encode("ascii"))])
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / BATCH_NAME).write_bytes(patch_batch(batch))


if __name__ == "__main__":
    main()
