"""Generate a source-dependent recipe and a reference archive from private build inputs.

The output recipe contains each retail record hash, followed by copies from
that record and XOR edits against it. The reference archive stays private.
"""
from hashlib import sha256
from pathlib import Path
import shutil
import struct
import sys

HEADER = b"KARl\x01\0\0\0\x10\0\0\0\0\x10\0\0"
BLOCK = 16


def unpack_aplib(encoded, size):
    bits = 0
    pending = 0
    index = 0
    output = bytearray()
    previous_distance = 0
    after_match = False

    def read():
        nonlocal index
        value = encoded[index]
        index += 1
        return value

    def bit():
        nonlocal bits, pending
        if not pending:
            bits = read()
            pending = 8
        pending -= 1
        return (bits >> pending) & 1

    def gamma():
        value = 1
        while True:
            value = 2 * value + bit()
            if value > 8192:
                raise ValueError("Invalid aPLib code")
            if not bit():
                return value

    output.append(read())
    while True:
        if not bit():
            output.append(read())
            after_match = False
            continue
        if not bit():
            high = gamma()
            if not after_match and high == 2:
                distance, count = previous_distance, gamma()
            else:
                distance = ((high - (2 if after_match else 3)) << 8) | read()
                count = gamma()
                count += 2 if distance < 128 or distance >= 32000 else int(distance >= 1280)
            previous_distance = distance
            after_match = True
        elif not bit():
            command = read()
            distance = command >> 1
            if not distance:
                break
            previous_distance = distance
            count = 2 + (command & 1)
            after_match = True
        else:
            distance = sum(bit() << shift for shift in range(3, -1, -1))
            if distance > len(output):
                raise ValueError("Invalid aPLib offset")
            output.append(output[-distance] if distance else 0)
            after_match = False
            continue
        if not 0 < distance <= len(output) or count > size - len(output):
            raise ValueError("Invalid aPLib match")
        if distance >= count:
            start = len(output) - distance
            output.extend(output[start:start + count])
        else:
            for _ in range(count):
                output.append(output[-distance])
    if len(output) != size or index != len(encoded):
        raise ValueError(f"Incomplete aPLib block ({len(output)}/{size}, {index}/{len(encoded)})")
    return output


def index_of(archive):
    archive.seek(0)
    header = archive.read(24)
    assert header[:16] == HEADER
    archive.seek(struct.unpack_from("<Q", header, 16)[0])
    count, _ = struct.unpack("<II", archive.read(8))
    return {key: (offset, size) for key, offset, size in
            struct.iter_unpack("<III", archive.read(count * 12))}


def read_entry(archive, entry):
    offset, size = entry
    archive.seek(offset * 16)
    record_size, blocks = struct.unpack("<II", archive.read(8))
    assert size == record_size
    if not blocks:
        raw = archive.read(size)
        assert len(raw) == size
        return raw
    assert blocks == (size + 4095) // 4096
    sizes = struct.unpack(f"<{blocks}I", archive.read(blocks * 4))
    data = bytearray()
    for length in sizes:
        data.extend(unpack_aplib(archive.read(length), min(size - len(data), 4096)))
    return bytes(data)


def difference(source, target):
    # Byte shifts in scripts and batches can place a source-aligned block at
    # any target offset. Compare from both sides before encoding an edit.
    blocks = {}
    for offset in range(0, len(source) - BLOCK + 1, BLOCK):
        blocks.setdefault(source[offset:offset + BLOCK], offset)
    edits = bytearray()
    cursor = 0
    pending = 0
    copied = 0

    def emit_xor(start, end):
        while start < end:
            base = start % len(source)
            count = min(end - start, len(source) - base)
            edits.extend(struct.pack(">BII", 2, base, count))
            edits.extend(a ^ b for a, b in zip(source[base:base + count], target[start:start + count]))
            start += count

    while cursor <= len(target) - BLOCK:
        if target[cursor:cursor + BLOCK] == source[cursor:cursor + BLOCK]:
            match = cursor
        else:
            match = blocks.get(target[cursor:cursor + BLOCK])
        if match is None:
            cursor += 1
            continue
        while cursor > pending and match > 0 and source[match - 1] == target[cursor - 1]:
            cursor -= 1
            match -= 1
        limit = min(len(source) - match, len(target) - cursor)
        lo = min(limit, BLOCK)
        hi = limit
        while lo < hi:
            length = lo + (hi - lo + 1) // 2
            if source[match + lo:match + length] == target[cursor + lo:cursor + length]:
                lo = length
            else:
                hi = length - 1
        emit_xor(pending, cursor)
        edits.extend(struct.pack(">BII", 1, match, lo))
        copied += lo
        cursor += lo
        pending = cursor
    emit_xor(pending, len(target))
    edits.append(0)
    return edits, copied


def digest_archive(file, offset):
    file.seek(offset)
    digest = sha256()
    while block := file.read(1024 * 1024):
        digest.update(block)
    return digest.hexdigest()


def build(game, current, recipe, metadata, reference):
    stock_patch = game / "data/patch_0.pak"
    stock_initial = game / "data/initial_0.pak"
    with stock_patch.open("rb") as patch, stock_initial.open("rb") as initial, current.open("rb") as old:
        retail_bytes = stock_patch.stat().st_size
        patch_index, initial_index, target_index = map(index_of, (patch, initial, old))
        changes = [(key, entry) for key, entry in target_index.items() if patch_index.get(key) != entry]
        assert 0 < len(changes) <= 128 and set(patch_index) <= set(target_index)
        patch.seek(0)
        copied_total = 0
        output_total = 0
        with recipe.open("wb") as instructions, reference.open("w+b") as pak:
            instructions.write(b"VEILED01" + struct.pack(">I", len(changes)))
            shutil.copyfileobj(patch, pak, 1024 * 1024)
            new_entries = dict(patch_index)
            for key, entry in changes:
                slot = int(key not in patch_index)
                original = (patch_index, initial_index)[slot][key]
                source = read_entry((patch, initial)[slot], original)
                target = read_entry(old, entry)
                edits, copied = difference(source, target)
                instructions.write(struct.pack(">IBII", key, slot, len(source), len(target)))
                instructions.write(sha256(source).digest())
                instructions.write(sha256(target).digest())
                instructions.write(edits)
                pak.write(bytes(-pak.tell() % 16))
                new_entries[key] = (pak.tell() // 16, len(target))
                pak.write(struct.pack("<II", len(target), 0))
                pak.write(target)
                copied_total += copied
                output_total += len(target)
            directory = pak.tell()
            pak.write(struct.pack("<II", len(new_entries), 0))
            for key, (offset, size) in sorted(new_entries.items()):
                pak.write(struct.pack("<III", key, offset, size))
            extent = pak.tell()
            pak.seek(16)
            pak.write(struct.pack("<Q", directory))
            pak.flush()
            metadata.write_text("\n".join([
                f"archive.bytes={extent}",
                f"archive.directory={directory}",
                f"archive.sha256={digest_archive(pak, 0)}",
                f"tail.sha256={digest_archive(pak, retail_bytes)}",
            ]) + "\n", encoding="ascii")
        print(f"{len(changes)} resources, {copied_total}/{output_total} output bytes copied directly from retail, "
              f"{recipe.stat().st_size} recipe bytes before compression")


if __name__ == "__main__":
    build(*(Path(arg) for arg in sys.argv[1:]))
