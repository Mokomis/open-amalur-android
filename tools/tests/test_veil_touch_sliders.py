"""Check the Android quantity-dialog edit against an independently supplied Veil batch."""
import os
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from amalur_script import parse_bytes
from mark_veil_portraits import constant_value
from patch_veil_touch_sliders import MANAGER, MARKER, patch_batch


def entries(batch):
    count, = struct.unpack_from("<I", batch)
    offset = 4 + 8 * count
    for kind, length in struct.iter_unpack("<II", batch[4:offset]):
        yield kind, batch[offset:offset + length]
        offset += length


def values(fn):
    return [constant_value(x) for x in fn["constants"]]


def accessed_fields(fn):
    # Follow both sides of original conditional instructions. Instructions
    # bypassed by the confirm fix must never touch a destroyed slider.
    instructions = fn["instructions"]
    pending = [0]
    reached = set()
    fields = []
    constants = values(fn)
    while pending:
        pc = pending.pop()
        if pc in reached or pc >= len(instructions):
            continue
        reached.add(pc)
        ins = instructions[pc]
        op = ins["opcode_name"]
        if op.startswith("GETFIELD"):
            fields.append((pc, constants[ins["C_raw"]]))
        if op == "JMP":
            pending.append(ins["branch_target_pc_provisional"])
        elif op != "RETURN":
            pending.append(pc + 1)
            if op in ("EQ", "LE", "TEST", "TEST_R1"):
                pending.append(pc + 2)
    return [name for _, name in sorted(fields)]


class VeilTouchSliders(unittest.TestCase):
    def test_reject_wrong_manager_or_incomplete_archive(self):
        for data in (struct.pack("<I", 0), struct.pack("<III", 1, MANAGER, 1) + b"x"):
            with self.assertRaises(ValueError):
                patch_batch(data)

    @unittest.skipUnless(os.environ.get("OPEN_AMALUR_VEIL_BATCH"), "Private Veil source batch not supplied")
    def test_existing_pointer_events_and_controller_paths(self):
        source = Path(os.environ["OPEN_AMALUR_VEIL_BATCH"]).read_bytes()
        before = dict(entries(source))
        after = dict(entries(patch_batch(source)))
        self.assertEqual(before.keys(), after.keys())
        self.assertEqual([k for k in after if before[k] != after[k]], [MANAGER])
        old, new = (parse_bytes(b[MANAGER])["chunk"]["root_function"]["children"] for b in (before, after))
        for index in (2, 3, 4, 7):
            self.assertEqual([x["word_hex"] for x in old[index]["instructions"]],
                             [x["word_hex"] for x in new[index]["instructions"]])
        for index in (0, 1):
            self.assertTrue({MARKER, "m_active_buttons", "m_button_strings", "lb", "rb", "-", "+"}
                            .issubset(values(new[index])))
        self.assertTrue({"a", "b", "dpad_left", "dpad_right", "lstick_left", "lstick_right"}
                        .issubset(values(new[5])))
        confirm = accessed_fields(new[5])
        self.assertNotIn("get_slider_value", confirm)
        self.assertEqual(confirm.count("current"), 2)
        pop = accessed_fields(new[6])
        self.assertLess(pop.index("get_slider_value"), pop.index("destroy_window"))


if __name__ == "__main__":
    unittest.main()
