"""Enable direct touch on the original bed and item-quantity dialogs."""
from hashlib import sha256
from struct import iter_unpack, pack, pack_into, unpack_from

from amalur_script import parse_bytes
from fit_veil_inventory import insert_callback
from mark_veil_portraits import Code, integer

MANAGER = 456318
MANAGER_SHA = "5ae3f94bb30741b9aca5245720c6dbd3d0b168e8e4d510b15a6c91e6ce819ebd"
MARKER = "VeilQuantitySlider"


def show_adjustments(fn):
    # Use the standard clickable Veil footer alongside its localized A/B actions.
    code = Code(fn)
    r = fn["maximum_stack_size"]
    code.glob(r, "m_active_buttons")
    code.op("LOADBOOL", r + 1, 1)
    for action in ("lb", "rb"):
        code.op("SETFIELD", r, code.index(action), r + 1)
    code.glob(r, "m_button_strings")
    for action, caption in (("lb", "-"), ("rb", "+")):
        code.load(r + 1, caption)
        code.op("SETFIELD", r, code.index(action), r + 1)
    return code, r + 2


def adjustment_input(fn):
    # Both the clickable button ID and the controller's shoulder press use
    # the existing one-step, range-checked change function and slider sound.
    code = Code(fn)
    r = fn["maximum_stack_size"]
    for index, (name, step) in enumerate((("lb", -1), ("lbumper_press", -1),
                                          ("rb", 1), ("rbumper_press", 1))):
        code.op("EQ", 0, 1, 256 + code.index(name))
        code.jump(f"next_{index}")
        code.glob(r, "change_slider_value")
        code.load(r + 1, step)
        code.op("CALL_I", r, 2, 1)
        code.op("RETURN", 0, 1)
        code.mark(f"next_{index}")
    return code, r + 2


def modal_marker(fn):
    # Both constructors and the pop path may be reached via scripting.
    # Preserve modal input if another original slider remains in the queue.
    code = Code(fn)
    r = fn["maximum_stack_size"]
    code.glob(r, "m_slider_info")
    code.op("GETTABLE_S", r, r, 256 + code.index(1))
    code.op("DATA")
    code.op("DATA")
    code.op("TEST", r)
    code.jump("empty")
    code.load(r + 2, "1")
    code.jump("write")
    code.mark("empty")
    code.load(r + 2, "0")
    code.mark("write")
    code.glob(r, "GAME")
    code.field(r, r, "set_personal_ini_entry_str")
    code.load(r + 1, MARKER)
    code.op("CALL_I", r, 3, 1)
    return code, r + 3


def remember_value(fn):
    # At this point table.remove has returned the dialog record in R0 but
    # its actual slider still exists. Keep the value used by both confirm paths.
    code = Code(fn)
    r = fn["maximum_stack_size"]
    code.native(r, "get_slider_value")
    code.field(r + 1, 0, "slider")
    code.op("CALL_I", r, 2, 2)
    code.op("SETFIELD", 0, code.index("current"), r)
    return code, r + 2


def confirm_value(script):
    # Replace two getters that run AFTER pop_slider_window destroys the widget.
    # Read the confirmed value stored on the returned dialog record instead.
    def edit(fn):
        code = Code(fn)
        code.field(4, 2, "current")
        return code, fn["maximum_stack_size"]

    # The original second callback used the variable result count from a native
    # getter. It now receives exactly one argument, just like the item callback
    # receives that value and its original object.
    fn = parse_bytes(script)["chunk"]["root_function"]["children"][5]
    b = bytearray(script)
    for pc, next_pc in ((41, 50), (58, 67)):
        words = Code(fn)
        words.op("JMP")
        jump = words.words[0] | ((65535 + next_pc - pc - 1) << 8)
        pack_into(">I", b, fn["instructions"][pc]["file_offset"], jump)
    fixed = Code(fn)
    fixed.op("CALL_I", 3, 2, 1)
    start = fn["instructions"][67]["file_offset"]
    b[start:start + 4] = integer(fixed.words[0])
    for pc in (58, 41):
        b = insert_callback(bytes(b), 5, pc, edit)
    return b


def patch_batch(batch):
    (count,) = unpack_from("<I", batch)
    table_end = 4 + 8 * count
    offset = table_end
    headers, entries = [], []
    found = False
    for key, size in iter_unpack("<II", batch[4:table_end]):
        script = batch[offset:offset + size]
        offset += size
        if key == MANAGER:
            if found or sha256(script).hexdigest() != MANAGER_SHA:
                raise ValueError("Unrecognized item and bed slider manager")
            found = True
            script = confirm_value(script)
            script = insert_callback(script, 5, 16, adjustment_input)
            for fn, open_pc, buttons_pc in ((0, 184, 160), (1, 190, 166)):
                script = insert_callback(script, fn, open_pc, modal_marker)
                script = insert_callback(script, fn, buttons_pc, show_adjustments)
            script = insert_callback(script, 6, 45, modal_marker)
            script = insert_callback(script, 6, 17, remember_value)
            parse_bytes(script)
        headers.append(pack("<II", key, len(script)))
        entries.append(script)
    if not found or offset != len(batch):
        raise ValueError("Incomplete slider batch")
    return batch[:4] + b"".join(headers + entries)
