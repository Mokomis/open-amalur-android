"""Bind Veil settings input to the widget that received the gesture."""

import veil_gameplay_choices as gameplay
from veil_choice_widgets import lookup
from veil_home_preview import home_layout, patch_home
from veil_depth_choices import CATEGORY_HINT, NAV_HINT_ID, layout_with_choices
import veil_depth_choices as sliders
from pathlib import Path
from zlib import crc32
from generate_veil_edits import index_of, read_entry
from struct import iter_unpack, pack, unpack_from

from amalur_script import parse_bytes
from mark_veil_portraits import Code, constant_value, insert
from patch_veil_environment import repack_script

FOCUS = "veil_set_options_focus"
PANE = "m_veil_settings_focus"
OWNERS = "m_veil_slider_rows"
EDITING = "m_veil_adjusting_value"


def global_set(code, name, register):
    code.wide("SETGLOBAL", register, code.index(name))


def method(code, base, name):
    code.op("SELF", base, base, 256 + code.index(name))


def focus(code, register, settings, editing=False):
    code.glob(register, FOCUS)
    code.op("LOADBOOL", register + 1, int(settings))
    code.op("LOADBOOL", register + 2, int(editing))
    code.op("CALL_I", register, 3, 1)


def set_row(code, register, source):
    code.glob(register, "set_selected_option")
    code.op("MOVE", register + 1, source)
    code.op("CALL_I", register, 2, 1)


def make_focus_function(ids):
    # Keep the guest's selection for scrolling, previews and value callbacks.
    # Only the pane accepting directional input paints its active control.
    code = Code({"constants": []})
    global_set(code, PANE, 0)
    global_set(code, EDITING, 1)
    code.native(2, "find_static")
    code.glob(3, "m_window")
    code.load(4, NAV_HINT_ID)
    code.load(5, -1)
    code.op("LOADBOOL", 6, 1)
    code.op("CALL_I", 2, 5, 2)
    code.op("TEST", 2)
    code.jump("list")
    code.native(3, "set_label_text")
    code.op("MOVE", 4, 2)
    code.load(5, CATEGORY_HINT)
    code.op("TEST", 0)
    code.jump("paint_hint")
    code.load(5, "Left / B   Categories\nA   Select")
    code.op("TEST", 1)
    code.jump("paint_hint")
    code.load(5, "Left/Right   Adjust\nA   Done\nB   Categories")
    code.mark("paint_hint")
    code.op("CALL_I", 3, 3, 1)
    code.mark("list")
    code.glob(1, "m_list")
    code.op("TEST", 1)
    code.jump("end")
    method(code, 1, "get_selected")
    code.op("CALL_I", 1, 2, 2)
    code.op("TEST", 1)
    code.jump("category")
    code.glob(2, "m_list")
    method(code, 2, "get_button")
    code.op("MOVE", 4, 1)
    code.op("CALL_I", 2, 3, 2)
    code.op("TEST", 2)
    code.jump("category")
    code.native(3, "highlight_button")
    code.op("MOVE", 4, 2)
    code.op("MOVE", 5, 0)
    code.op("CALL_I", 3, 3, 1)
    code.mark("category")
    code.glob(1, "m_title_helper")
    code.field(1, 1, "m_cur_tab")
    code.op("NEWTABLE", 2)
    for index, value in enumerate(ids):
        code.load(3 + index, value)
    code.op("SETLIST", 2, len(ids), 1)
    lookup(code, 2, 2, 1)
    code.op("TEST", 2)
    code.jump("end")
    code.native(3, "find_static")
    code.glob(4, "m_window")
    code.op("MOVE", 5, 2)
    code.load(6, -1)
    code.op("LOADBOOL", 7, 1)
    code.op("CALL_I", 3, 5, 2)
    code.op("TEST", 3)
    code.jump("end")
    code.native(4, "set_visible")
    code.op("MOVE", 5, 3)
    code.op("NOT", 6, 0)
    code.op("CALL_I", 4, 3, 1)
    code.mark("end")
    code.op("RETURN", 0, 1)
    words = code.finish()
    return (
        pack(">6IBII", 0, 0, 0, 0, 0, 2, 0, 10, len(words) // 4)
        + words
        + pack(">I", len(code.values))
        + code.new_constants
        + bytes(16)
    )


def add_focus_function(data, ids):
    parsed = parse_bytes(data)
    root = parsed["chunk"]["root_function"]
    children = root["children"]
    count = len(children)
    start, end = children[0]["file_offset"] - 4, children[-1]["end_file_offset"]
    callbacks = [
        (sliders.adapter_name(name), sliders.choice_index(name), 8)
        for name in sliders.ORDERED
    ]
    callbacks.extend(
        [
            (sliders.adapter_name(sliders.BOUNDING), sliders.bounding_getter(), 8),
            ("veil_set_camera_bounding", sliders.bounding_setter(), 8),
        ]
    )
    callbacks.append(("veil_adapt_options_row", sliders.row_adapter(), 16))
    callbacks.append(("on_veil_game_toggle", gameplay.callback(None), 12))
    callbacks.extend(
        (f"on_veil_hud_{index}", gameplay.callback(index), 16) for index in range(1, 4)
    )
    choices = []
    for _, cb, stack in callbacks:
        words = cb.finish()
        choices.append(
            pack(">6IBII", 0, 0, 0, 0, 0, 4, 0, stack, len(words) // 4)
            + words
            + pack(">I", len(cb.values))
            + cb.new_constants
            + bytes(16)
        )
    data = repack_script(
        data,
        parsed,
        data[:start]
        + pack(">I", count + 1 + len(choices))
        + data[start + 4 : end]
        + make_focus_function(ids)
        + b"".join(choices)
        + data[end:],
    )
    root = parse_bytes(data)["chunk"]["root_function"]
    code = Code(root)
    code.wide("CLOSURE", 0, count)
    global_set(code, FOCUS, 0)
    for index, (name, _, _) in enumerate(callbacks):
        code.wide("CLOSURE", 0, count + 1 + index)
        global_set(code, name, 0)
    a, b, new = insert(data, root, len(root["instructions"]) - 1, code, 1)
    return repack_script(data, parsed, data[:a] + new + data[b:])


def patch_options(data):
    parsed = parse_bytes(data)
    root = parsed["chunk"]["root_function"]
    if FOCUS.encode() in data:
        raise ValueError("Veil input changes already present")
    children = root["children"]
    const = lambda fn: list(map(constant_value, fn["constants"]))
    # The guest selects the Veil slider colors using two guards in the paint
    # routine. Apply that palette to every slider, in both focus states.
    paint = children[125]
    palette_sites = [
        op["pc"]
        for op in paint["instructions"]
        if op["opcode_name"] == "GETFIELD"
        and op["A"] == 16
        and const(paint)[op["C_raw"]] == "veil_active"
    ]
    if len(palette_sites) != 2:
        raise ValueError("Unrecognized Veil slider palette")
    for site in palette_sites:
        offset = paint["code_file_offset"] + 4 * site
        palette = Code(paint)
        palette.op("LOADBOOL", 16, 1)
        palette.op("MOVE", 16, 16)
        palette.op("MOVE", 16, 16)
        data = data[:offset] + palette.finish() + data[offset + 12 :]
    tabs = children[121]
    # Each sidebar label is followed by a static selection background with a
    # set_visible(tab == index) call; collect its ID from the existing layout.
    ids = []
    ops = tabs["instructions"]
    for i, op in enumerate(ops):
        if op["opcode_name"] == "NOT" or i < 10:
            continue
        if (
            op["opcode_name"] == "EQ"
            and ops[i - 1]["opcode_name"] == "LOADBOOL"
            and ops[i + 3]["opcode_name"] == "CALL_I"
        ):
            id_ins = ops[i - 11]
            if id_ins["opcode_name"] == "LOADK":
                ids.append(const(tabs)[id_ins["Bx_raw"]])
    if len(ids) != 6 or const(children[147])[7] != "BEGIN_CHANGE":
        raise ValueError(f"Unrecognized options layout: {ids}")
    edits = []

    def edit(fn, pc, emit, stack=24):
        code = Code(children[fn])
        emit(code)
        a, b, new = insert(data, children[fn], pc, code, stack)
        edits.append((a, b, new))

    def reset_rows(c):
        c.op("LOADNIL", 15, 15)
        global_set(c, "m_veil_previous_option", 15)
        c.glob(15, PANE)
        c.op("TEST", 15)
        c.jump("reset")
        c.glob(15, "m_list")
        c.op("TEST", 15)
        c.jump("reset")
        method(c, 15, "get_selected")
        c.op("CALL_I", 15, 2, 2)
        c.op("TEST", 15)
        c.jump("reset")
        c.glob(16, "m_cur_options")
        lookup(c, 16, 16, 15)
        c.op("TEST", 16)
        c.jump("reset")
        c.field(16, 16, "id")
        global_set(c, "m_veil_previous_option", 16)
        c.mark("reset")
        c.op("NEWTABLE", 15)
        global_set(c, OWNERS, 15)

    # Functions with more than one edit must be reparsed between insertions.
    def bind(c):
        c.glob(13, OWNERS)
        for handle in (1, 8):
            c.op("SETTABLE_S", 13, handle, 0)

    def hud_choice(c):
        request_hud_refresh(c, 7)
        c.op("EQ", 1, 3, 256 + c.index(2))
        c.jump("done")
        c.glob(7, "GameState")
        c.field(7, 7, "set_is_ui_hidden")
        c.op("LOADBOOL", 8, 0)
        c.op("CALL_I", 7, 2, 1)
        c.mark("done")

    edit(9, 42, hud_choice)
    edit(101, 114, bind)

    def bind_param(c):
        c.glob(13, OWNERS)
        c.op("SETTABLE_S", 13, 1, 0)

    edit(112, 131, bind_param)
    # Ordered sliders keep the original params for the engine's callbacks.
    # On a later init their getter already returns a slider index. Route them
    # through the stock integer initializer before it tries to read index.param.
    init = children[117]
    init_ops = init["instructions"]
    (params_test,) = (
        op["pc"] + 4
        for op in init_ops
        if op["opcode_name"] == "LOADK"
        and op["A"] == 18
        and const(init)[op["Bx_raw"]] == "params"
        and init_ops[op["pc"] + 4]["opcode_name"] == "TEST"
    )

    def init_selection(c):
        c.load(26, "int_range")
        lookup(c, 26, 17, 26)
        c.op("TEST", 26)
        c.jump("done")
        c.op("LOADNIL", 18, 18)
        c.mark("done")

    edit(117, params_test, init_selection, 27)
    edit(125, 37, sliders.numeric_label)
    edit(126, 16, gameplay.sync_current_choice)

    def slider(c):
        c.glob(4, OWNERS)
        c.op("TEST", 4)
        c.jump("ignore")
        lookup(c, 5, 4, 1)
        c.op("TEST", 5)
        c.jump("ignore")
        set_row(c, 6, 5)
        focus(c, 6, True)
        c.jump("done")
        c.mark("ignore")
        c.op("RETURN", 0, 1)
        c.mark("done")

    edit(147, 11, slider)

    def repaint_slider(c):
        c.op("EQ", 1, 0, 256 + c.index("CHANGING"))
        c.jump("paint")
        c.op("EQ", 0, 0, 256 + c.index("END_CHANGE"))
        c.jump("done")
        c.mark("paint")
        c.glob(4, "update_visibility")
        c.op("CALL_I", 4, 1, 1)
        c.op("RETURN", 0, 1)
        c.mark("done")

    # Insert after the guest applies the value. Reparse to retain its branches.

    def pointer(c):
        c.op("EQ", 1, 0, 256 + c.index("DOWN"))
        c.jump("press")
        c.op("EQ", 0, 0, 256 + c.index("CLICKED"))
        c.jump("done")
        c.mark("press")
        c.glob(8, "m_list")
        method(c, 8, "get_button_id")
        c.op("MOVE", 10, 1)
        c.op("CALL_I", 8, 3, 2)
        c.op("TEST", 8)
        c.jump("arrow")
        set_row(c, 9, 8)
        focus(c, 9, True)
        c.jump("done")
        c.mark("arrow")
        c.glob(8, "m_arrows")
        method(c, 8, "get_button_id")
        c.op("MOVE", 10, 1)
        c.op("CALL_I", 8, 3, 2)
        c.op("TEST", 8)
        c.jump("done")
        c.glob(9, "string")
        c.field(9, 9, "match")
        c.op("MOVE", 10, 8)
        c.load(11, "^(%d+)_")
        c.op("CALL_I", 9, 3, 2)
        c.op("TEST", 9)
        c.jump("done")
        c.glob(10, "tonumber")
        c.op("MOVE", 11, 9)
        c.op("CALL_I", 10, 2, 2)
        set_row(c, 11, 10)
        focus(c, 11, True)
        c.mark("done")

    edit(150, 34, pointer)

    def events(c):
        c.field(3, 0, "info")

        def matches(actions, label):
            for action in actions:
                c.op("EQ", 1, 3, 256 + c.index(action))
                c.jump(label)

        c.glob(4, PANE)
        c.op("TEST", 4)
        c.jump("left-pane")
        matches(("b", "bbutton", "cancelevent"), "leave")
        c.glob(4, EDITING)
        c.op("TEST", 4)
        c.jump("browse")
        matches(("a", "abutton", "selectevent"), "finish-edit")
        matches(
            (
                "up",
                "padupevent",
                "down",
                "paddownevent",
                "rbumper_press",
                "lbumper_press",
                "rb",
                "lb",
                "majortableft",
                "majortabright",
            ),
            "browse-again",
        )
        c.jump("done")  # Left and Right adjust the selected value while editing.
        c.mark("browse-again")
        focus(c, 4, True)
        c.jump("done")
        c.mark("finish-edit")
        focus(c, 4, True)
        c.jump("handled")
        c.mark("browse")
        matches(("left", "padleftevent"), "leave")
        matches(("right", "padrightevent", "a", "abutton", "selectevent"), "select")
        c.jump("done")
        c.mark("select")
        c.glob(4, "m_list")
        method(c, 4, "get_selected")
        c.op("CALL_I", 4, 2, 2)
        c.op("TEST", 4)
        c.jump("done")
        c.glob(5, "m_cur_options")
        lookup(c, 5, 5, 4)
        c.op("TEST", 5)
        c.jump("done")
        c.field(6, 5, gameplay.WIDGET)
        c.op("EQ", 0, 6, 256 + c.index("toggle"))
        c.jump("normal-select")
        matches(("a", "abutton", "selectevent"), "toggle-value")
        c.jump("normal-select")
        c.mark("toggle-value")
        c.glob(6, "change_internal_option")
        c.load(7, 1)
        c.op("CALL_I", 6, 2, 1)
        gameplay.refresh(c, 6)
        c.jump("handled")
        c.mark("normal-select")
        for field in ("int_range", "params"):
            c.field(6, 5, field)
            c.op("TEST", 6, 0, 1)
            c.jump("edit-value")
        c.jump("done")  # Action buttons keep their stock handler.
        c.mark("edit-value")
        focus(c, 4, True, editing=True)
        c.jump("handled")
        c.mark("left-pane")
        # Consume further Left repeats after returning from a row. The stock
        # handler would otherwise decrement the hidden row selection.
        matches(("left", "padleftevent"), "handled")
        matches(("right", "padrightevent", "a", "abutton", "selectevent"), "enter")
        matches(("up", "padupevent"), "previous")
        matches(("down", "paddownevent"), "next")
        c.jump("done")
        for label, action in (("previous", "majortableft"), ("next", "majortabright")):
            c.mark(label)
            c.op("SETFIELD", 0, c.index("info"), 256 + c.index(action))
            c.jump("done")
        c.mark("enter")
        focus(c, 4, True)
        c.jump("handled")
        c.mark("leave")
        focus(c, 4, False)
        c.mark("handled")
        c.op("SETFIELD", 0, c.index("handled"), 256 + c.index(True))
        c.op("RETURN", 0, 1)
        c.mark("done")

    edit(139, 357, events)
    for fn in range(168, 174):
        edit(fn, 8, lambda c: focus(c, 9, False))

    # The footer leaves the page even if a row editor had focus. The physical
    # B button still steps back to the category list first.
    edit(174, 8, lambda c: focus(c, 9, False))

    def show(c):
        c.op("LOADBOOL", 4, 0)
        global_set(c, PANE, 4)
        global_set(c, EDITING, 4)
        c.glob(4, "GAME")
        c.field(4, 4, "set_personal_ini_entry_num")
        c.load(5, "VeilOptionsScriptInput")
        c.load(6, 1)
        c.op("CALL_I", 4, 3, 1)

    edit(146, 2, show)
    edit(121, 0, reset_rows)
    for a, b, new in sorted(edits, reverse=True):
        data = data[:a] + new + data[b:]
    data = repack_script(data, parsed, data)
    parsed = parse_bytes(data)
    # These functions already contain insertions; use the updated instruction
    # offsets to preserve the original jumps and the pointer/controller paths.
    for child, site, emitter in (
        (147, None, repaint_slider),
        (121, None, sliders.prepare_row),
        (112, 12, gameplay.select_param_template),
        (150, 0, gameplay.arrow_press),
        (125, None, gameplay.paint_param),
        (125, None, sliders.suppress_inline_restart),
    ):
        fn = parsed["chunk"]["root_function"]["children"][child]
        if emitter == repaint_slider:
            site = [
                op["pc"]
                for op in fn["instructions"]
                if op["opcode_name"] == "EQ"
                and op["C_raw"] >= 256
                and const(fn)[op["C_raw"] - 256] == "END_CHANGE"
            ][-1]
        elif emitter == sliders.prepare_row:
            site = next(
                op["pc"]
                for op in fn["instructions"]
                if op["opcode_name"] == "GETFIELD"
                and op["B_raw"] == 8
                and const(fn)[op["C_raw"]] == "int_range"
            )
        elif emitter == sliders.suppress_inline_restart:
            (site,) = (
                op["pc"]
                for op in fn["instructions"]
                if op["opcode_name"] == "MOVE"
                and op["A"] == 12
                and op["B_raw"] == 8
                and fn["instructions"][op["pc"] + 1]["opcode_name"] == "CALL_I"
            )
        elif site is None:
            (site,) = (
                op["pc"]
                for op in fn["instructions"]
                if op["opcode_name"] == "TEST"
                and op["A"] == 7
                and fn["instructions"][op["pc"] - 1]["opcode_name"] == "CALL_I_R1"
            )
        extension = Code(fn)
        emitter(extension)
        a, b, replacement = insert(data, fn, site, extension, 24)
        data = repack_script(data, parsed, data[:a] + replacement + data[b:])
        parsed = parse_bytes(data)
    fn = parsed["chunk"]["root_function"]["children"][121]
    # The visible option list has just been filtered for device, input and menu
    # type. Move the calibration pair together before its windows get indices.
    walk = Code(fn)
    walk.op("EQ", 0, 0, 256 + walk.index("Graphics"))
    walk.jump("done")
    walk.glob(11, "m_cur_options")
    walk.op("LEN", 12, 11)
    walk.load(13, 1)  # read index
    walk.load(14, 1)  # write index
    walk.op("LOADNIL", 17, 18)
    walk.mark("read")
    walk.op("LE", 0, 13, 12)
    walk.jump("append")
    lookup(walk, 15, 11, 13)
    walk.field(16, 15, "id")
    for option, register in (("brightness", 17), ("brightness_bink", 18)):
        walk.op("EQ", 0, 16, 256 + walk.index(option))
        walk.jump("skip-" + option)
        walk.op("MOVE", register, 15)
        walk.jump("next")
        walk.mark("skip-" + option)
    walk.op("SETTABLE_S", 11, 14, 15)
    walk.op("ADD", 14, 14, 256 + walk.index(1))
    walk.mark("next")
    walk.op("ADD", 13, 13, 256 + walk.index(1))
    walk.jump("read")
    walk.mark("append")
    # Scrolling follows the selected row. Place the preview above the final
    # slider so controller users can see the reference while adjusting it.
    for register in (18, 17):
        walk.op("TEST", register)
        walk.jump("missing-" + str(register))
        walk.op("SETTABLE_S", 11, 14, register)
        walk.op("ADD", 14, 14, 256 + walk.index(1))
        walk.mark("missing-" + str(register))
    walk.mark("done")
    sliders.restart_footer(walk)
    (before_windows,) = (
        i["pc"]
        for i in fn["instructions"]
        if i["opcode_name"] == "GETGLOBAL_MEM"
        and const(fn)[i["Bx_raw"]] == "ipairs"
        and fn["instructions"][i["pc"] - 1]["opcode_name"] == "FORLOOP"
    )
    a, b, new = insert(data, fn, before_windows, walk, 19)
    data = repack_script(data, parsed, data[:a] + new + data[b:])
    parsed = parse_bytes(data)
    fn = parsed["chunk"]["root_function"]["children"][121]
    restore = Code(fn)
    restore.glob(10, "m_veil_previous_option")
    restore.op("TEST", 10)
    restore.jump("done")
    restore.glob(11, "m_cur_options")
    restore.load(12, 1)
    restore.mark("row")
    lookup(restore, 13, 11, 12)
    restore.op("TEST", 13)
    restore.jump("done")
    restore.field(13, 13, "id")
    restore.op("EQ", 0, 13, 10)
    restore.jump("next")
    restore.op("MOVE", 6, 12)
    restore.jump("done")
    restore.mark("next")
    restore.op("ADD", 12, 12, 256 + restore.index(1))
    restore.jump("row")
    restore.mark("done")
    select = next(
        i["pc"]
        for i in fn["instructions"]
        if i["opcode_name"] == "SELF" and const(fn)[i["C_raw"] - 256] == "set_selected"
    )
    a, b, new = insert(data, fn, select + 2, restore, 16)
    data = repack_script(data, parsed, data[:a] + new + data[b:])
    parsed = parse_bytes(data)
    fn = parsed["chunk"]["root_function"]["children"][121]
    code = Code(fn)
    code.glob(2, FOCUS)
    code.glob(3, PANE)
    code.op("NOT", 3, 3)
    code.op("NOT", 3, 3)
    code.glob(4, EDITING)
    code.op("CALL_I", 2, 3, 1)
    a, b, new = insert(data, fn, len(fn["instructions"]) - 1, code, 5)
    return add_focus_function(
        repack_script(data, parsed, data[:a] + new + data[b:]), ids
    )


HUD_PENDING = "veil_refresh_hud_on_resume"


def request_hud_refresh(c, register):
    c.glob(register, "InGameUIStartup")
    c.op("LOADBOOL", register + 1, 1)
    c.op("SETFIELD", register, c.index(HUD_PENDING), register + 1)


def patch_gameplay_hud(data):
    parsed = parse_bytes(data)
    children = parsed["chunk"]["root_function"]["children"]
    event, update = children[13:15]
    (show_pc,) = (
        op["pc"]
        for op in event["instructions"]
        if op["opcode_name"] == "EQ"
        and op["C_raw"] >= 256
        and constant_value(event["constants"][op["C_raw"] - 256]) == "SHOW"
    )
    (frame_pc,) = (
        op["pc"]
        for op in update["instructions"]
        if op["opcode_name"] == "GETGLOBAL_MEM"
        and constant_value(update["constants"][op["Bx_raw"]]) == "update_player_bars"
    )
    shown = Code(event)
    request_hud_refresh(shown, 3)
    # SHOW schedules work. Only the visible gameplay update owns the HUD layers.
    c = Code(update)
    c.glob(3, "InGameUIStartup")
    c.field(4, 3, HUD_PENDING)
    c.op("TEST", 4)
    c.jump("done")
    c.glob(4, "PROFILE")
    c.field(4, 4, "get_variable")
    c.load(5, 11308144)
    c.op("CALL_I", 4, 2, 2)
    c.field(5, 3, "m_current_display_type")
    c.op("EQ", 1, 4, 5)
    c.jump("visibility")
    c.field(5, 3, "update_hud_display_type")
    c.op("CALL_I", 5, 1, 1)
    c.mark("visibility")
    # An independent hide-UI command still takes precedence until the player
    # explicitly selects a visible size in Options.
    c.glob(5, "GameState")
    c.field(5, 5, "get_is_ui_hidden")
    c.op("CALL_I", 5, 1, 2)
    c.op("TEST", 5, 0, 1)
    c.jump("applied")
    c.glob(5, "script_window_registry")
    c.field(5, 5, "hide_ui_via_cheat")
    c.op("LOADBOOL", 6, 0)
    c.op("EQ", 0, 4, 256 + c.index(2))
    c.jump("move")
    c.op("LOADBOOL", 6, 1)
    c.mark("move")
    c.op("CALL_I", 5, 2, 1)
    c.mark("applied")
    c.op("LOADBOOL", 4, 0)
    c.op("SETFIELD", 3, c.index(HUD_PENDING), 4)
    c.mark("done")
    edits = [
        insert(data, event, show_pc + 2, shown, 6),
        insert(data, update, frame_pc, c, 8),
    ]
    for a, b, new in sorted(edits, reverse=True):
        data = data[:a] + new + data[b:]
    return repack_script(data, parsed, data)


def patch_batch(batch):
    (count,) = unpack_from("<I", batch)
    offset = 4 + count * 8
    records, headers = [], []
    patched = set()
    for key, size in iter_unpack("<II", batch[4:offset]):
        record = batch[offset : offset + size]
        offset += size
        if key in (13493, 13523, 1402689):
            if key in patched:
                raise ValueError(f"Duplicate menu script {key}")
            patched.add(key)
        if key == 1402689:
            record = patch_gameplay_hud(record)
        if key == 13493:
            record = patch_home(record)
        if key == 13523:
            record = patch_options(record)
            parse_bytes(record)

        records.append(record)
        headers.append(pack("<II", key, len(record)))
    if patched != {13493, 13523, 1402689} or offset != len(batch):
        raise ValueError("Unexpected Veil script batch")
    return batch[:4] + b"".join(headers) + b"".join(records)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Generate the Home and Options replacements from a local Veil installation."
    )
    parser.add_argument("archive", type=Path, help="Existing Veil patch_0.pak")
    parser.add_argument(
        "output", type=Path, help="Directory for the three replacement assets"
    )
    parser.add_argument(
        "--batch",
        type=Path,
        help="Use a newer, locally generated script batch (for example dynamic resolution)",
    )
    args = parser.parse_args()
    edits = {
        "134230570_klua.batch": patch_batch,
        "1621447.ui_bxml": home_layout,
        "1650829.ui_bxml": layout_with_choices,
    }
    with args.archive.open("rb") as archive:
        index = index_of(archive)
        outputs = {}
        for name, patch in edits.items():
            source = (
                args.batch.read_bytes()
                if args.batch and name.endswith(".batch")
                else read_entry(archive, index[crc32(name.encode("ascii"))])
            )
            outputs[name] = patch(source)
    args.output.mkdir(parents=True, exist_ok=True)
    for name, result in outputs.items():
        (args.output / name).write_bytes(result)
