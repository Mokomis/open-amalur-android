"""Mark visible original player portraits for the Android waterfall compositor."""
import argparse
from hashlib import sha256
from pathlib import Path
from struct import iter_unpack, pack, pack_into, unpack_from
from zlib import crc32

from amalur_script import OPCODE_NAMES, parse_bytes
from generate_veil_edits import index_of, read_entry


def integer(value):
    return pack(">I", value)


def constant_value(entry):
    if entry["type"] == "number":
        return entry["integer_value"]
    value = entry["value"]
    return value["text"] if entry["type"] == "string" else value


class Code:
    def __init__(self, function):
        self.words = []
        self.values = [constant_value(c) for c in function["constants"]]
        self.new_constants = bytearray()
        self.labels = {}
        self.branches = {}

    def index(self, value):
        for i, candidate in enumerate(self.values):
            if type(candidate) is type(value) and candidate == value:
                return i
        self.values.append(value)
        if value is None:
            self.new_constants.append(0)
        elif type(value) is int:
            self.new_constants.extend(b"\x03" + pack(">i", value))
        else:
            value_bytes = value.encode("ascii") + b"\0"
            self.new_constants.extend(b"\x04" + integer(len(value_bytes)) + value_bytes)
        return len(self.values) - 1

    def op(self, name, a=0, b=0, c=0):
        assert 0 <= a <= 255 and 0 <= b <= 255 and 0 <= c <= 511
        self.words.append((OPCODE_NAMES.index(name) << 25) | (b << 17) | (c << 8) | a)

    def wide(self, name, a, bx):
        self.op(name, a, bx >> 9, bx & 511)

    def load(self, register, value):
        self.wide("LOADK", register, self.index(value))

    def glob(self, register, name):
        symbol = self.index(name)
        cache = len(self.values)
        self.values.extend((None, None))
        self.new_constants.extend(b"\0\0")
        self.wide("GETGLOBAL_MEM", register, symbol)
        self.wide("DATA", 20, cache)

    def field(self, dest, source, name):
        self.op("GETFIELD", dest, source, self.index(name))
        self.op("DATA")
        self.op("DATA")

    def jump(self, label):
        self.branches[len(self.words)] = label
        self.op("JMP")

    def mark(self, label):
        self.labels[label] = len(self.words)

    def native(self, dest, method):
        self.glob(dest, "WINDOW")
        self.field(dest, dest, method)

    def finish(self):
        for pc, label in self.branches.items():
            displacement = self.labels[label] - pc - 1
            self.words[pc] |= (65535 + displacement) << 8
        return b"".join(map(integer, self.words))


def insert(data, fn, pc, code, stack):
    instructions = fn["instructions"]
    end = fn["code_file_offset"] + len(instructions) * 4
    added = code.finish()
    count = len(added) // 4
    original = bytearray(data[fn["code_file_offset"]:end])
    for ins in instructions:
        target = ins.get("branch_target_pc_provisional")
        if target is None:
            continue
        old_pc = ins["pc"]
        # An existing branch to the inserted code should execute it.
        relative = target + (count if target > pc else 0) - old_pc - (count if old_pc >= pc else 0) - 1
        original_word = int(ins["word_hex"], 16)
        pack_into(">I", original, old_pc * 4,
                  (original_word & 0xfe0000ff) | ((65535 + relative) << 8))
    children = fn["children"]
    const_end = children[0]["file_offset"] - 4 if children else fn["end_file_offset"] - 16
    assert data[const_end:const_end + 4] == integer(len(children))
    start = fn["code_file_offset"]
    payload = (
        integer(max(stack, fn["maximum_stack_size"]))
        + integer(len(instructions) + count)
        + original[:pc * 4] + added + original[pc * 4:]
        + integer(len(code.values)) + data[end + 4:const_end] + code.new_constants
    )
    return start - 8, const_end, payload

def ledger(data):
    # The original ledger keeps its portrait visible when switching from the
    # summary to an inventory category. Bypass only the summary predicate.
    fn = parse_bytes(data)["chunk"]["root_function"]["children"][47]
    begin, *_, end = fn["instructions"][191:202]
    if begin["word_hex"] != "aa00220a" or end["word_hex"] != "aa00100a":
        raise ValueError("Unrecognized ledger portrait control flow")
    branch = Code(fn)
    branch.jump("update")
    branch.labels["update"] = end["pc"] - begin["pc"]
    start = begin["file_offset"]
    return data[:start] + branch.finish() + data[start + 4:]


def other_portrait(data, child, skip_race=False):
    parsed = parse_bytes(data)
    fn = parsed["chunk"]["root_function"]["children"][child]
    code = Code(fn)
    if skip_race:
        code.glob(10, "m_cur_phase")
        code.load(11, 1)  # Race is the first entry in m_char_gen_phases.
        code.op("EQ", 0, 10, 11)
        code.jump("portrait")
        code.load(12, "0")
        code.jump("send")
        code.mark("portrait")
    code.glob(10, "m_portrait_wrapper")
    code.op("TEST", 10)
    code.jump("done")
    code.native(11, "is_visible")
    code.op("MOVE", 12, 10)
    code.op("CALL_I", 11, 2, 2)
    code.op("TEST", 11)
    code.jump("done")

    code.load(13, "rect,")
    for i, getter in enumerate(("get_left", "get_top", "get_width", "get_height")):
        dest = 14 + i * 2
        code.native(dest, getter)
        code.op("MOVE", dest + 1, 10)
        code.op("CALL_I", dest, 2, 2)
        if i < 3:
            code.load(dest + 1, ",")
    code.op("CONCAT", 12, 13, 20)
    code.mark("send")
    code.glob(10, "GAME")
    code.field(10, 10, "set_personal_ini_entry_str")
    code.load(11, "VeilWaterfall")
    code.op("CALL_I", 10, 3, 1)
    code.mark("done")
    start, end, replacement = insert(data, fn, 0, code, 22)
    output = data[:start] + replacement + data[end:]
    offset = parsed["wrapper"]["payload_file_offset"]
    return output[:offset - 4] + pack("<I", len(output) - offset) + output[offset:]


def patch_batch(data):
    count, = unpack_from("<I", data)
    offset = 4 + count * 8
    directory = []
    members = []
    edits = {13520: ledger, 256627: lambda payload: other_portrait(payload, 24),
             417426: lambda payload: other_portrait(payload, 28, skip_race=True)}
    for name, length in iter_unpack("<II", data[4:offset]):
        payload = data[offset:offset + length]
        offset += length
        if name in edits:
            payload = edits.pop(name)(payload)
            parse_bytes(payload)
        directory.append(pack("<II", name, len(payload)))
        members.append(payload)
    if edits or offset != len(data):
        raise ValueError("Missing portrait scripts in the Veil UI batch")
    return data[:4] + b"".join(directory) + b"".join(members)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    name = "134230570_klua.batch"
    with args.archive.open("rb") as pak:
        data = read_entry(pak, index_of(pak)[crc32(name.encode("ascii"))])
    if sha256(data).hexdigest() != "2975f5cbf477f210a92463ae6f76562cc0a0b8f4d87bd28f254186e82a36ff7c":
        raise ValueError("Unrecognized Veil UI script batch")
    args.output.mkdir(exist_ok=True)
    (args.output / name).write_bytes(patch_batch(data))


if __name__ == "__main__":
    main()
