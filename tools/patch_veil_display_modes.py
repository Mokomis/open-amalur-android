"""Populate Veil's existing resolution selector from Android's display modes."""
import argparse
from pathlib import Path
from struct import iter_unpack, pack, unpack_from
from amalur_script import parse_bytes
from mark_veil_portraits import Code, constant_value, insert


def patch_options(data):
    parsed = parse_bytes(data)
    root = parsed['chunk']['root_function']
    values = [constant_value(c) for c in root['constants']]
    if 'VeilResolutionCount' in values:
        raise ValueError('Display modes are already installed')
    starts = [i['pc'] for i in root['instructions'] if i['opcode_name'] == 'LOADK'
              and values[i['Bx_raw']] == 'veil_render_resolution']
    if len(starts) != 1:
        raise ValueError('Unrecognized resolution selector')
    pc = next(i['pc'] for i in root['instructions'][starts[0]:]
              if i['opcode_name'] == 'CALL_I' and i['A'] == 0 and i['B_raw'] == 4)
    if pc - starts[0] != 50:
        raise ValueError('Unrecognized resolution selector layout')
    code = Code(root)
    # Registers 0..3 hold table.insert, the section, position and option row.
    # Replace the old fixed arrays immediately before that row is inserted.
    code.op('NEWTABLE', 4)
    code.op('NEWTABLE', 5)
    code.load(6, 0)

    def getter(dest, key, fallback, index=None):
        code.glob(dest, 'GAME')
        code.load(dest + 1, 'get_personal_ini_entry_num')
        code.op('GETTABLE_S', dest, dest, dest + 1)
        code.op('DATA'); code.op('DATA')
        code.load(dest + 1, key)
        if index is not None:
            code.op('MOVE', dest + 2, index)
            code.op('CONCAT', dest + 1, dest + 1, dest + 2)
        code.load(dest + 2, fallback)
        code.op('CALL_I', dest, 3, 2)

    getter(7, 'VeilResolutionCount', 3)
    code.mark('next')
    code.op('LT', 0, 6, 7)
    code.jump('done')
    code.op('ADD', 14, 6, code.index(1) + 256)
    code.op('SETTABLE_N', 4, 14, 6)
    getter(8, 'VeilResolutionWidth', 1920, 6)
    getter(10, 'VeilResolutionHeight', 1080, 6)
    code.load(9, 'x')
    code.op('CONCAT', 8, 8, 10)
    code.op('ADD', 14, 6, code.index(1) + 256)
    code.op('SETTABLE_N', 5, 14, 8)
    code.op('ADD', 6, 6, code.index(1) + 256)
    code.jump('next')
    code.mark('done')
    code.load(8, 'params')
    code.op('SETTABLE_S', 3, 8, 4)
    code.load(8, 'display_vals')
    code.op('SETTABLE_S', 3, 8, 5)
    code.wide('SETGLOBAL', 5, code.index('m_veil_resolution_labels'))
    start, end, replacement = insert(data, root, pc, code, 16)
    result = data[:start] + replacement + data[end:]
    offset = parsed['wrapper']['payload_file_offset']
    result = result[:offset - 4] + pack('<I', len(result) - offset) + result[offset:]
    parse_bytes(result)
    return result


def patch_batch(data):
    count, = unpack_from('<I', data)
    offset = 4 + count * 8
    headers, payloads = [], []
    changed = 0
    for name, size in iter_unpack('<II', data[4:offset]):
        payload = data[offset:offset + size]
        offset += size
        if name == 13523:
            payload = patch_options(payload)
            changed += 1
        headers.append(pack('<II', name, len(payload)))
        payloads.append(payload)
    if offset != len(data) or changed != 1:
        raise ValueError('Invalid Veil batch')
    return data[:4] + b''.join(headers) + b''.join(payloads)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('batch', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_bytes(patch_batch(args.batch.read_bytes()))


if __name__ == '__main__':
    main()
