"""Hide the shared portrait while a character detail screen is open."""

from pathlib import Path
from struct import iter_unpack, pack, pack_into, unpack_from

from amalur_script import parse_bytes

LEDGER_SCRIPT = 13520
ORIGINAL_PREDICATE = 0xAA00220A
ALWAYS_SHOW_PORTRAIT = 0x39000800


def fix_ledger_script(data):
    parsed = parse_bytes(data)
    instruction = parsed["chunk"]["root_function"]["children"][47]["instructions"][191]
    current = int(instruction["word_hex"], 16)
    if current == ORIGINAL_PREDICATE:
        return data
    if current != ALWAYS_SHOW_PORTRAIT:
        raise ValueError("Unrecognized ledger portrait predicate")
    output = bytearray(data)
    pack_into(">I", output, instruction["file_offset"], ORIGINAL_PREDICATE)
    parse_bytes(output)
    return bytes(output)


def fix_batch(data):
    count, = unpack_from("<I", data)
    offset = 4 + count * 8
    headers, records = [], []
    seen = 0
    for key, size in iter_unpack("<II", data[4:offset]):
        record = data[offset:offset + size]
        if key == LEDGER_SCRIPT:
            record = fix_ledger_script(record)
            seen += 1
        headers.append(pack("<II", key, len(record)))
        records.append(record)
        offset += size
    if offset != len(data) or seen != 1:
        raise ValueError("Malformed Veil menu batch")
    return data[:4] + b"".join(headers) + b"".join(records)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(fix_batch(args.input.read_bytes()))
