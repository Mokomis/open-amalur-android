"""Fit Veil inventory and Options layouts, and connect the original Item Radial."""

import argparse
from hashlib import sha256
from pathlib import Path
from struct import iter_unpack, pack, pack_into, unpack_from
from zlib import crc32

from amalur_script import parse_bytes
from fit_veil_panels import BOTTOM, HEIGHT, Layout, LEFT, RELATIVE_RIGHT, RELATIVE_WIDTH, RIGHT, TOP, WIDTH
from generate_veil_edits import index_of, read_entry
from mark_veil_name_dialog import BATCH_NAME
from mark_veil_portraits import Code, constant_value, insert


def inventory_list(data):
    layout = Layout(data)
    # Keep a margin between the selected row and the edge of its list panel.
    layout.set(67, {RELATIVE_WIDTH: 272})
    for node in (13, 59):
        layout.set(node, {RIGHT: 12})
    layout.set(16, {WIDTH: 222})
    layout.set(26, {RIGHT: 0})
    layout.set(43, {RIGHT: 29, BOTTOM: 1})
    layout.set(82, {RELATIVE_RIGHT: 161})
    return layout.encode()


def item_details(data):
    layout = Layout(data)
    width = 296
    layout.set(52, {WIDTH: width})
    for node in (13, 20, 29):
        layout.set(node, {WIDTH: 194})
    for node in (92, 114, 116, 130):
        layout.set(node, {921821: width})
    layout.set(100, {LEFT: 16, 921821: 152})
    layout.set(108, {LEFT: 152, 921821: 288})
    layout.set(125, {LEFT: 20, 921821: width - 20})
    layout.set(132, {WIDTH: width - 22})
    for node in (296, 298, 300):
        layout.set(node, {WIDTH: width})
    for node in (302, 304):
        layout.set(node, {WIDTH: width - 2})
    layout.set(308, {LEFT: width - 3})
    layout.set(310, {WIDTH: width - 60})
    return layout.encode()


def options_navigation(data):
    layout = Layout(data)
    background = 117
    last = layout.nodes[background][1]
    while layout.nodes[last][2] != -1:
        last = layout.nodes[last][2]

    label = len(layout.nodes)
    layout.nodes[last][2] = label
    layout.nodes.extend(([background, label + 1, -1, 0, 0],
                         [label, -1, label + 2, 0, 0],
                         [label, -1, -1, 0, 0]))
    layout.attributes.extend(layout.attributes[index][:] for index in (360, 361, 362))
    layout.set(label, {3971938: "veil_category_controls", 900629: "LB / RB   Category",
                       LEFT: 62, TOP: 456, WIDTH: 232, HEIGHT: 34})
    layout.set(label + 2, {27695: 171, 15212: 147, 96669: 100})
    return layout.encode()


def show_footer_buttons(data, child, sites):
    output = bytearray(data)
    fn = parse_bytes(data)["chunk"]["root_function"]["children"][child]
    on = next(c["index"] for c in fn["constants"] if constant_value(c) == "on")
    for pc in sites:
        ins = fn["instructions"][pc]
        if ins["opcode_name"] != "LOADK" or constant_value(fn["constants"][ins["Bx_raw"]]) != "pc_dropdown":
            raise ValueError("Unrecognized footer action")
        word = int(ins["word_hex"], 16)
        pack_into(">I", output, ins["file_offset"], (word & 0xfe0000ff) | (on << 8))
    return bytes(output)


def fit_inventory_actions(data):
    # Mapping, reading and native Equip/Unequip are also available without a mouse.
    data = show_footer_buttons(data, 30, (479, 1009, 1403, 1453, 1483))

    def offset_panel(fn, register):
        code = Code(fn)
        code.op("SUB", register, register, code.index(74) + 256)
        return code, fn["maximum_stack_size"]

    data = insert_callback(data, 48, 82, lambda fn: offset_panel(fn, 3))
    for pc, x in ((579, 9), (554, 9), (399, 5)):
        data = insert_callback(data, 61, pc, lambda fn: offset_panel(fn, x))
    return data


def insert_callback(data, index, pc, write_code):
    parsed = parse_bytes(data)
    fn = parsed["chunk"]["root_function"]["children"][index]
    code, stack = write_code(fn)
    start, end, replacement = insert(data, fn, pc, code, stack)
    data = data[:start] + replacement + data[end:]
    offset = parsed["wrapper"]["payload_file_offset"]
    return data[:offset - 4] + pack("<I", len(data) - offset) + data[offset:]


def mark_window(data, child, key, show_pc, hide_pc):
    for pc, value in sorted(((hide_pc, "0"), (show_pc, "1")), reverse=True):
        def edit(fn):
            register = fn["maximum_stack_size"]
            code = Code(fn)
            code.glob(register, "GAME")
            code.field(register, register, "set_personal_ini_entry_str")
            code.load(register + 1, key)
            code.load(register + 2, value)
            code.op("CALL_I", register, 3, 1)
            return code, register + 3
        data = insert_callback(data, child, pc, edit)
    return data


def mark_radial(data):
    data = mark_window(data, 16, "VeilItemRadial", 13, 126)
    return insert_callback(show_footer_buttons(data, 5, (82, 97)), 23, 18, select_radial_slot)


def mark_map(data):
    return mark_window(data, 35, "VeilMap", 6, 170)


def select_radial_slot(fn):
    # Android can deliver the mouse move and click in the same frame. Resolve
    # the clicked slot before running the stock confirmation and eligibility checks.
    ring, buttons, _, id_arg = range(fn["maximum_stack_size"], fn["maximum_stack_size"] + 4)
    code = Code(fn)
    code.glob(ring, "m_outer_ring")
    code.field(buttons, ring, "buttons")
    code.op("SELF", buttons, buttons, code.index("get_button_id") + 256)
    code.op("MOVE", id_arg, 1, 0)
    code.op("CALL_I", buttons, 3, 2)
    code.op("SETFIELD", ring, code.index("selected"), buttons)
    code.glob(ring, "provide_update")
    code.op("LOADBOOL", buttons, 1, 0)
    code.op("CALL_I", ring, 2, 1)
    return code, id_arg + 1


def select_ledger_row(fn):
    tree, rows, index, row, call, _, arg2, arg3 = range(
        fn["maximum_stack_size"], fn["maximum_stack_size"] + 8
    )
    code = Code(fn)
    code.op("EQ", 0, 0, code.index("CLICKED") + 256)
    code.jump("stock")
    code.op("TEST", 2)
    code.jump("left_click")
    code.op("EQ", 0, 2, code.index(0) + 256)
    code.jump("stock")

    # The click can arrive before the hover callback updates the highlight.
    code.mark("left_click")
    code.op("TEST", 1)
    code.jump("done")
    code.glob(tree, "m_button_tree")
    code.op("SELF", call, tree, code.index("is_root") + 256)
    code.op("CALL_I", call, 2, 2)
    code.op("TEST", call)
    code.jump("stock")
    code.field(rows, tree, "m_ui")
    code.field(rows, rows, "repeater_wins")
    code.load(index, 1)
    code.mark("row")
    code.op("GETTABLE_S", row, rows, index)
    code.op("DATA")
    code.op("DATA")
    code.op("TEST", row)
    code.jump("done")
    code.field(call, row, "button")
    code.op("EQ", 1, call, 1)
    code.jump("click")
    code.op("ADD", index, index, code.index(1) + 256)
    code.jump("row")

    code.mark("click")
    code.op("SELF", call, tree, code.index("set_highlight") + 256)
    code.op("MOVE", arg2, index)
    code.op("CALL_I", call, 3, 1)
    code.op("SELF", call, tree, code.index("handle_clicked") + 256)
    code.op("MOVE", arg2, 1)
    code.op("MOVE", arg3, 2)
    code.op("CALL_I", call, 4, 1)
    code.mark("done")
    code.op("RETURN", 0, 1)
    code.mark("stock")
    return code, arg3 + 1


def fix_character_touch(data):
    return insert_callback(data, 49, 5, select_ledger_row)


def add_hud_shortcut(data):
    def edit(fn):
        register = fn["maximum_stack_size"]
        code = Code(fn)
        code.load(register, 6)
        code.op("EQ", 1, 2, register)
        code.jump("open")
        code.load(register, "6")
        code.op("EQ", 0, 2, register)
        code.jump("done")
        code.mark("open")
        code.glob(register, "usable_items_radial")
        code.field(register, register, "open_radial")
        code.op("CALL_I", register, 1, 1)
        code.mark("done")
        return code, register + 1

    return insert_callback(data, 14, 134, edit)


def fix_tutorial_text(data):
    lessons = (
        (35926227, "Some reagents are harder to harvest than others. "
                   "Improve your Alchemy skill to harvest them more reliably."),
        (24620006, "You found a reagent. Use reagents to make potions at an Alchemy workbench."),
        (21980198, "Choose Create to brew a potion from a recipe you know. "
                   "Choose Experiment to mix reagents and discover new recipes."),
        (82674331, "Select reagents to add them to your mixture. Add at least two, then choose "
                   "Experiment to brew. You can remove the last reagent before mixing."),
        (68250781, "Use the forge to craft equipment from components or salvage equipment for parts."),
        (47899806, "Choose an item and select Salvage to recover components. This consumes the original item."),
        (65758380, "Choose the equipment you want to make, then select components for its crafting slots."),
        (91425686, "Choose a component for the highlighted slot. The selected components determine the item's stats."),
        (88622818, "Review the recovered components, then choose Continue to return to the forge."),
        (79090488, "Review your crafted item, then choose Continue to return to the forge."),
    )

    def edit(fn):
        register = fn["maximum_stack_size"]
        code = Code(fn)
        code.glob(register, "m_curr_tutorial")
        for lesson, _ in lessons:
            code.op("EQ", 1, register, code.index(lesson) + 256)
            code.jump(lesson)
        code.jump("formatted")
        # Render these lessons through the plain text path.
        for lesson, help_text in lessons:
            code.mark(lesson)
            code.load(7, help_text)
            code.jump("plain")
        # Preserve the original formatted path for the other lessons.
        code.mark("formatted")
        code.labels["plain"] = len(code.words) + (238 - 226)
        return code, register + 1

    # Delay showing the timed gameplay panel until it contains text. If text
    # rendering throws, the player no longer sees an empty backdrop.
    def reveal_timed_panel(fn):
        register = fn["maximum_stack_size"]
        code = Code(fn)
        code.glob(register, "m_timed_tutorial")
        code.op("TEST", register)
        code.jump("done")
        code.native(register, "set_visible")
        code.op("MOVE", register + 1, 4)
        code.op("LOADBOOL", register + 2, 1)
        code.op("CALL_I", register, 3, 1)
        code.mark("done")
        return code, register + 3

    def show_untimed_panel(fn):
        code = Code(fn)
        code.glob(9, "m_timed_tutorial")
        code.op("NOT", 9, 9)
        return code, fn["maximum_stack_size"]

    data = insert_callback(data, 1, 246, reveal_timed_panel)
    data = insert_callback(data, 1, 226, edit)
    return insert_callback(data, 1, 152, show_untimed_panel)


def death_menu_touch(data):
    def edit(fn):
        tree, rows, row, index, button, x, y, call, arg1, arg2 = range(
            fn["maximum_stack_size"], fn["maximum_stack_size"] + 10
        )
        code = Code(fn)

        def report(value):
            code.glob(call, "GAME")
            code.field(call, call, "set_personal_ini_entry_str")
            code.load(arg1, "VeilDeathMenu")
            code.load(arg2, value)
            code.op("CALL_I", call, 3, 1)

        code.glob(tree, "m_button_tree")
        code.op("TEST", tree)
        code.jump("inactive")
        code.op("SELF", call, tree, code.index("is_root") + 256)
        code.op("CALL_I", call, 2, 2)
        code.op("TEST", call)
        code.jump("inactive")
        report("1")
        code.glob(call, "GAME")
        code.field(call, call, "get_personal_ini_entry_num")
        code.load(arg1, "VeilDeathMenuTap")
        code.load(arg2, 0)
        code.op("CALL_I", call, 3, 2)
        code.op("EQ", 1, call, code.index(1) + 256)
        code.jump("hit_test")
        code.op("EQ", 0, call, code.index("1") + 256)
        code.jump("done")

        code.mark("hit_test")
        code.field(rows, tree, "m_ui")
        code.field(rows, rows, "repeater_wins")
        code.load(index, 1)
        code.mark("row")
        code.op("GETTABLE_S", row, rows, index)
        code.op("DATA")
        code.op("DATA")
        code.op("TEST", row)
        code.jump("done")
        code.field(button, row, "button")
        code.native(call, "is_visible")
        code.op("MOVE", arg1, button)
        code.op("CALL_I", call, 2, 2)
        code.op("TEST", call)
        code.jump("next_row")
        code.native(call, "get_mouse_pos_local_to_window")
        code.op("MOVE", arg1, button)
        code.op("CALL_I", call, 2, 3)
        code.op("MOVE", x, call)
        code.op("MOVE", y, arg1)
        for point, dimension in ((x, "get_width"), (y, "get_height")):
            code.op("LT", 1, point, code.index(0) + 256)
            code.jump("next_row")
            code.native(call, dimension)
            code.op("MOVE", arg1, button)
            code.op("CALL_I", call, 2, 2)
            code.op("LT", 0, point, call)
            code.jump("next_row")

        report("0")
        code.op("SELF", call, tree, code.index("set_highlight") + 256)
        code.op("MOVE", arg2, index)
        code.op("CALL_I", call, 3, 1)
        code.glob(call, "on_inputdeviceaction_event")
        code.op("LOADNIL", arg1, arg1)
        code.load(arg2, "a")
        code.op("CALL_I", call, 3, 1)
        code.op("RETURN", 0, 1, 0)

        code.mark("next_row")
        code.op("ADD", index, index, code.index(1) + 256)
        code.jump("row")
        code.mark("inactive")
        report("0")
        code.mark("done")
        return code, arg2 + 1

    return insert_callback(data, 17, 15, edit)


def patch_batch(batch):
    count, = unpack_from("<I", batch)
    offset = 4 + count * 8
    edits = {
        13503: ("306d628f5a36c327df4fe9de83b7c53945a51831e32007255a7b7d722555beba", death_menu_touch),
        13517: ("97640493b79252e5021fe7926d350cf8b07e2ef77ec9e99e6625c8256fd9e22b", fit_inventory_actions),
        13519: ("11d6e7136727c5f5f4de9893a3213fa7c67db1eb5d6cf1ebe6840ae6d2c29a20", mark_map),
        13520: ("4101f7f763fddb3237fbfe521cbfb7e5079c35b1b6502d0c918c7b15e10a1a4c", fix_character_touch),
        249501: ("c3e3ded912e0b540f37355d5c40b016c1ccec8de08e477b395b510f986ca8e00", mark_radial),
        733459: ("2f6d2b0cce3818b49458bb7227c6a9e9809dd80f6946cfa15dec594ea8bf0682", fix_tutorial_text),
        1402689: ("4742faad4b62de5c170c4a260751a62d9b4babde6a8e8a857accf409159f2b8e", add_hud_shortcut),
    }
    directory, members = [], []
    for resource, size in iter_unpack("<II", batch[4:offset]):
        script = batch[offset:offset + size]
        offset += size
        if resource in edits:
            expected, edit = edits.pop(resource)
            if sha256(script).hexdigest() != expected:
                raise ValueError(f"Unrecognized input script {resource}")
            script = edit(script)
            parse_bytes(script)
        directory.append(pack("<II", resource, len(script)))
        members.append(script)
    if edits or offset != len(batch):
        raise ValueError("Missing scripts for the Item Radial")
    return batch[:4] + b"".join(directory) + b"".join(members)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with args.archive.open("rb") as pak:
        index = index_of(pak)
        layouts = (
            ("1653751.ui_bxml", "dbc9232275c3f30ff19e44666569943aee9ff8c24fe78a5e885c06e41bae18d9", inventory_list),
            ("1621434.ui_bxml", "e5e71867b3db7c866f86b037c004df4144b008123d3a01b432638665e8e3ad88", item_details),
            ("1650829.ui_bxml", "f3edf7abefc7ccc27752f539f983d6cd85b5c8e09805e61e1cbaa278cd5502fa", options_navigation),
        )
        batch = read_entry(pak, index[crc32(BATCH_NAME.encode("ascii"))])
        (args.output / BATCH_NAME).write_bytes(patch_batch(batch))
        for name, expected, edit in layouts:
            data = read_entry(pak, index[crc32(name.encode("ascii"))])
            if sha256(data).hexdigest() != expected:
                raise ValueError(f"Unrecognized input layout {name}")
            (args.output / name).write_bytes(edit(data))


if __name__ == "__main__":
    main()
