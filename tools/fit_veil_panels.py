"""Fit Veil's quest, save and item detail panels to the game viewport."""
import argparse
from hashlib import sha256
from pathlib import Path
import struct
from zlib import crc32

from generate_veil_edits import index_of, read_entry

LEFT, RIGHT, TOP, BOTTOM = 581899, 9073508, 354013, 10777313
WIDTH, HEIGHT = 897369, 1453632
NODE = struct.Struct("<5i")
WORD = struct.Struct("<I")


class Layout:
    def __init__(self, data):
        offset = 0

        def read_table(entry):
            nonlocal offset
            count, = WORD.unpack_from(data, offset)
            start = offset + WORD.size
            offset = start + count * entry.size
            return list(entry.iter_unpack(data[start:offset]))

        self.nodes = [list(node) for node in read_table(NODE)]
        attributes = [value for value, in read_table(WORD)]
        self.symbols = [value for value, in read_table(WORD)]
        count, = WORD.unpack_from(data, offset)
        offset += WORD.size
        self.values = []
        for _ in range(count):
            length, = WORD.unpack_from(data, offset)
            offset += WORD.size
            self.values.append(data[offset:offset + length])
            offset += length
        self.trailer = data[offset:]
        self.attributes = [attributes[start:start + count] for *_, count, start in self.nodes]

    def set(self, node, changes):
        attrs = self.attributes[node]
        attrs[:] = [word for word in attrs if self.symbols[word & 0xfff] not in changes]
        for key, value in changes.items():
            if value is None:
                continue
            if key not in self.symbols:
                self.symbols.append(key)
            raw = str(value).encode("ascii")
            if raw not in self.values:
                self.values.append(raw)
            attrs.append((96 << 24) | (self.values.index(raw) << 12) | self.symbols.index(key))

    def add_backdrop(self, parent, shape, tint):
        rect, color = len(self.nodes), len(self.nodes) + 1
        self.nodes.extend(([parent, color, self.nodes[parent][1], 0, 0],
                           [rect, -1, -1, 0, 0]))
        self.attributes.extend((self.attributes[shape][:], self.attributes[tint][:]))
        self.nodes[parent][1] = rect
        self.set(rect, {LEFT: 0, RIGHT: 0, TOP: 0, BOTTOM: 0, WIDTH: None, HEIGHT: None})
        self.set(color, {27695: 0, 15212: 0, 96669: 0, 90168: 255})

    def encode(self):
        def table(fmt, records):
            return WORD.pack(len(records)) + b"".join(fmt.pack(*record) for record in records)

        words = []
        for node, attrs in zip(self.nodes, self.attributes):
            node[-2:] = [len(attrs), len(words)]
            words.extend((word,) for word in attrs)
        if max(len(self.symbols), len(self.values)) > 4096:
            raise ValueError("Layout exceeds its 12-bit symbol or value index")
        return (table(NODE, self.nodes) + table(WORD, words)
                + table(WORD, [(key,) for key in self.symbols]) + WORD.pack(len(self.values))
                + b"".join(WORD.pack(len(raw)) + raw for raw in self.values) + self.trailer)


def quest(layout):
    layout.add_backdrop(1, 125, 126)
    layout.set(96, {LEFT: 62, WIDTH: 365})
    for node in (125, 127):
        layout.set(node, {LEFT: 48, WIDTH: 393})
    layout.set(100, {LEFT: 478, RIGHT: 62, WIDTH: None})
    for node in (129, 131):
        layout.set(node, {LEFT: 464, RIGHT: 48, WIDTH: None})


def saves(layout):
    layout.set(96, {RIGHT: 0, WIDTH: None})
    for node in (113, 173, 175, 177):
        layout.set(node, {RIGHT: 424, WIDTH: None})
    for node in (115, 179, 181, 183):
        layout.set(node, {LEFT: None, RIGHT: 52, WIDTH: 340})
    for node in (169, 171, 185, 187):
        layout.set(node, {RIGHT: 0, WIDTH: None})
    layout.set(125, {WIDTH: 292, HEIGHT: 164})
    for node in (116, 117, 119, 121, 123, 138):
        layout.set(node, {RIGHT: 22, WIDTH: None})
    layout.set(152, {RIGHT: 68, WIDTH: None})
    layout.set(156, {LEFT: None, RIGHT: 71, WIDTH: 345})


def details(layout):
    # The equipped status sits beside the raised item icon.
    layout.set(296, {TOP: -22, HEIGHT: 544})
    layout.set(298, {TOP: -22})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(exist_ok=True)
    layouts = (
        ("1661673.ui_bxml", "80787b4742d81aef68f691c3de4bfcfb09b031aec79fced4a043d3d312bf8694", quest),
        ("1621469.ui_bxml", "6608a9298c2fc3b5d9243da5c27443c7349750158e7c1a0e53318ba92e89fe14", saves),
        ("1621434.ui_bxml", "7acf825de3e0d26c872cdfb12e81cb1824741305f4a0d5f88f75dfe65ee6ab17", details),
    )
    with args.archive.open("rb") as pak:
        index = index_of(pak)
        for name, expected_hash, edit in layouts:
            data = read_entry(pak, index[crc32(name.encode("ascii"))])
            if sha256(data).hexdigest() != expected_hash:
                raise ValueError(f"Unrecognized input layout {name}")
            layout = Layout(data)
            if layout.encode() != data:
                raise ValueError(f"Layout did not round trip for {name}")
            edit(layout)
            (args.output / name).write_bytes(layout.encode())


if __name__ == "__main__":
    main()
