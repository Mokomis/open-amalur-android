"""Shared Options controls built from the original Veil row and button templates."""

from math import sqrt

from fit_veil_panels import LEFT, RIGHT, TOP, WIDTH, HEIGHT
from mark_veil_portraits import Code
from veil_choice_widgets import ID, TEXT, CALLBACK, MARKS, lookup, visibility

TOGGLES = (
    "autosave",
    "hidehelmet",
    "tutorials",
    "minimap",
    "questmarkers",
    "subtitles",
    "VeilNightAmbience",
    "VeilDayAmbience",
    "VeilDynamicResolution",
    "VeilResolutionHeatResponse",
    "vsync",
    "postproc",
    "vegetation",
    "vibration",
    "invertcamera",
    "auto_follow",
    "enemy_focus",
    "veil_camera_bounding",
)
NAMED = (
    "huddisplay",
    "texquality",
    "veil_window_mode",
    "sprintcontrol",
    "sprintcontrol_pc",
    "antialiasing",
)
HUD_LABELS = (
    ("display", 2706402),
    ("veil_shadow_value", 17695542),
    ("veil_shadow_restart", 22643237),
)
WIDGET = "veil_gameplay_widget"
OWNERS = "m_veil_slider_rows"


def add_templates(layout, append, clone):
    # The headings and list share these anchors on every Options tab.
    layout.set(261, {TOP: 102, HEIGHT: 35})
    layout.set(
        264,
        {
            RIGHT: None,
            WIDTH: None,
            26550078: 95,
            9443087: 135,
            TOP: 102,
            HEIGHT: 35,
            957724: "Center",
        },
    )
    for font in (262, 265):
        layout.set(font, {679773: "font_4_large"})
    layout.set(132, {TOP: 151, HEIGHT: 470})

    def template(tag, symbol):
        mapping = {}
        node = clone(0, 56, mapping)
        if symbol not in layout.symbols:
            layout.symbols.append(symbol)
        attrs = layout.attributes[node]
        attrs[0] = (attrs[0] & ~4095) | layout.symbols.index(symbol)
        layout.set(node, {ID: tag})
        # The stock builder still registers its arrow handles.
        for index in (61, 67):
            layout.set(
                mapping[index], {LEFT: -4000, RIGHT: None, 26550078: None, WIDTH: 1}
            )
        return node, mapping[75], mapping[58]

    def group(parent, name, right, width):
        node = append(parent, 267)
        layout.set(
            node, {ID: name, LEFT: None, RIGHT: right, TOP: 0, WIDTH: width, HEIGHT: 48}
        )
        return node

    def box(parent, name, left, top, width, height, rgb):
        node = append(parent, 276)
        layout.set(node, {ID: name, LEFT: left, TOP: top, WIDTH: width, HEIGHT: height})
        color = append(node, 277)
        layout.set(color, dict(zip((27695, 15212, 96669, 90168), rgb)))

        return node

    def capsule(parent, name, left, top, width, height, color):
        # Veil has a solid rectangle primitive. Combine horizontal bands into
        # semicircular ends, retaining a single window for state visibility.
        layer = append(parent, 267)
        layout.set(
            layer, {ID: name, LEFT: left, TOP: top, WIDTH: width, HEIGHT: height}
        )
        radius = height / 2
        for y in range(0, height, 2):
            inset = round(radius - sqrt(radius**2 - (y + 1 - radius) ** 2))
            box(layer, name + "_band_" + str(y), inset, y, width - 2 * inset, 2, color)
        return layer

    def button(parent, name, callback, left, width):
        node = append(parent, 268)
        layout.set(node, {ID: name, LEFT: left, TOP: 0, WIDTH: width, HEIGHT: 48})
        for model in range(269, 276):
            child = append(node, model)
            if model == 270:
                layout.set(child, {CALLBACK: callback})

    toggle, value, highlight = template("veil_selected_1", 13351030)
    layout.set(
        value,
        {
            LEFT: None,
            RIGHT: 128,
            26550078: None,
            9443087: None,
            WIDTH: 150,
            957724: "Right",
        },
    )
    note = append(toggle, 414)
    layout.set(note, {TEXT: " "})
    append(note, 415)
    append(note, 416)
    track = group(toggle, "veil_toggle", 15, 186)
    # Give idle and selected rows the same bounds. Only the tint changes.
    row_bounds = {LEFT: 0, RIGHT: 0, WIDTH: None, TOP: 6, HEIGHT: 37}
    base = box(toggle, "veil_switch_row_base", 0, 0, 1, 1, (22, 27, 27, 255))
    layout.set(base, row_bounds)
    # Draw the idle surface behind the game's focus window and button.
    first = layout.nodes[toggle][1]
    tail = first
    while layout.nodes[tail][2] != base:
        tail = layout.nodes[tail][2]
    layout.nodes[tail][2] = -1
    layout.nodes[base][2] = first
    layout.nodes[toggle][1] = base
    # The stock focus style paints inside its window with an eight-unit inset.
    layout.set(highlight, {LEFT: 0, RIGHT: 0, TOP: -2, 10777313: None, HEIGHT: 53})
    capsule(track, "veil_switch_rim", 86, 9, 80, 32, (145, 135, 108, 255))
    capsule(track, "veil_switch_idle", 88, 11, 76, 28, (46, 52, 50, 255))
    capsule(track, MARKS[0][0], 88, 11, 76, 28, (129, 107, 59, 255))
    capsule(track, MARKS[1][0], 92, 14, 22, 22, (213, 207, 191, 255))
    capsule(track, MARKS[2][0], 138, 14, 22, 22, (250, 236, 195, 255))
    button(track, "veil_toggle_button", "on_veil_game_toggle", 0, 186)

    def choice_row(tag, symbol, widths):
        hud, old_value, _ = template(tag, symbol)
        layout.set(
            old_value,
            {LEFT: -4000, RIGHT: None, 26550078: None, 9443087: None, WIDTH: 1},
        )
        choices = group(hud, "veil_hud_choices", 12, sum(widths))
        left = 0
        for i, width in enumerate(widths):
            text = append(choices, 280)
            layout.set(
                text,
                {
                    ID: HUD_LABELS[i][0],
                    TEXT: " ",
                    LEFT: left,
                    TOP: 0,
                    WIDTH: width,
                    HEIGHT: 45,
                    957724: "Center",
                },
            )
            append(text, 281)
            append(text, 282)
            box(choices, MARKS[i][0], left + 6, 44, width - 12, 3, (220, 193, 127, 255))
            button(
                choices,
                "veil_hud_button_" + str(i + 1),
                "on_veil_hud_" + str(i + 1),
                left,
                width,
            )
            left += width

    choice_row("veil_selected_2", 12031017, (112, 94, 114))
    choice_row("veil_selected_6", 12123215, (135, 135))
    choice_row("new_cinematic", 10834273, (145, 180, 145))


def select_param_template(c):
    # add_param_win R0 is the actual row index, R3 the template name.
    c.glob(12, "m_cur_options")
    lookup(c, 12, 12, 0)
    c.field(13, 12, "id")
    for name in TOGGLES:
        c.op("EQ", 1, 13, 256 + c.index(name))
        c.jump("toggle")
    for name in NAMED:
        c.op("EQ", 1, 13, 256 + c.index(name))
        c.jump("named")
    c.jump("done")
    c.mark("named")
    c.load(3, "VEIL_SELECTED_2")
    c.field(14, 12, "params")
    c.op("LEN", 14, 14)
    c.op("EQ", 0, 14, 256 + c.index(2))
    c.jump("three")
    c.load(3, "VEIL_SELECTED_6")
    c.mark("three")
    c.op("EQ", 0, 13, 256 + c.index("veil_window_mode"))
    c.jump("size_done")
    c.load(3, "NEW_CINEMATIC")
    c.mark("size_done")
    c.load(14, "hud")
    c.jump("tag")
    c.mark("toggle")
    c.load(3, "VEIL_SELECTED_1")
    c.load(14, "toggle")
    c.mark("tag")
    c.op("SETFIELD", 12, c.index(WIDGET), 14)
    c.mark("done")


def find_element(c, symbol, method="find_static"):
    c.native(16, method)
    c.op("MOVE", 17, 5)
    c.load(18, symbol)
    c.load(19, -1)
    c.op("LOADBOOL", 20, 1)
    c.op("CALL_I", 16, 5, 2)


def paint_param(c):
    # update_visibility has the live getter result in R7, row window in R5.
    c.field(14, 4, WIDGET)
    c.op("TEST", 14)
    c.jump("end")
    c.op("TEST", 7)
    c.jump("end")
    c.field(15, 7, "param")
    c.op("EQ", 0, 14, 256 + c.index("toggle"))
    c.jump("hud")
    # Some original options use Lua booleans, others use numeric 0/1.
    c.op("LOADBOOL", 14, 0)
    c.op("TEST", 15)
    c.jump("switch")
    c.op("EQ", 1, 15, 256 + c.index(0))
    c.jump("switch")
    c.op("LOADBOOL", 14, 1)
    c.mark("switch")
    for i in range(3):
        find_element(c, MARKS[i][1], "find_window")
        c.op("NOT" if i == 1 else "MOVE", 15, 14)
        visibility(c, 16, 15, 17)
    c.field(14, 4, "id")
    c.op("EQ", 0, 14, 256 + c.index("veil_camera_bounding"))
    c.jump("end")
    c.field(14, 4, "veil_active")
    c.op("CALL_I", 14, 1, 2)
    c.field(15, 7, "param")
    c.load(19, " ")
    c.op("EQ", 1, 14, 15)
    c.jump("note")
    c.load(19, "Restart to apply")
    c.mark("note")
    c.op("MOVE", 14, 19)
    find_element(c, 22643237)
    c.native(17, "set_label_text")
    c.op("MOVE", 18, 16)
    c.op("MOVE", 19, 14)
    c.op("CALL_I", 17, 3, 1)
    c.jump("end")
    c.mark("hud")
    for i, (_, label_id) in enumerate(HUD_LABELS):
        if i == 2:
            c.field(18, 4, "params")
            c.op("LEN", 18, 18)
            c.op("LT", 1, 18, 256 + c.index(3))
            c.jump("end")
        find_element(c, label_id)
        c.native(17, "set_label_text")
        c.op("MOVE", 18, 16)
        c.field(19, 4, "display_vals")
        c.load(20, i + 1)
        lookup(c, 19, 19, 20)
        c.op("TEST", 19, 0, 1)
        c.jump("label" + str(i))
        c.load(19, " ")
        c.mark("label" + str(i))
        c.op("CALL_I", 17, 3, 1)
        find_element(c, MARKS[i][1])
        c.op("LOADBOOL", 14, 0)
        c.field(18, 4, "params")
        c.load(19, i + 1)
        lookup(c, 18, 18, 19)
        c.op("EQ", 0, 15, 18)
        c.jump("paint" + str(i))
        c.op("LOADBOOL", 14, 1)
        c.mark("paint" + str(i))
        visibility(c, 16, 14, 17)
    c.mark("end")


def sync_current_choice(c):
    # The original row can retain its default or a stale index when the active
    # input device changes. Derive the step from the same value the UI displays.
    # Scope this to live Gameplay preferences; pending display-mode choices
    # elsewhere continue to use the original confirmation behavior.
    c.field(9, 1, "int_range")
    c.op("TEST", 9)
    c.jump("params")
    c.field(9, 1, "get")
    c.op("CALL_I", 9, 1, 2)
    c.op("SETFIELD", 1, c.index("selected"), 9)
    c.jump("done")
    c.mark("params")
    c.field(9, 1, WIDGET)
    c.op("TEST", 9, 0, 1)
    c.jump("sync")
    c.field(9, 1, "id")
    c.op("EQ", 1, 9, 256 + c.index("textlanguage"))
    c.jump("sync")
    c.op("EQ", 0, 9, 256 + c.index("audiolanguage"))
    c.jump("done")
    c.mark("sync")
    c.field(9, 1, "get")
    c.op("CALL_I", 9, 1, 2)
    c.op("TEST", 9)
    c.jump("done")
    c.field(9, 9, "param")
    c.op("EQ", 0, 9, 256 + c.index(None))
    c.jump("scan")
    # An unset language preference is the original Auto entry.
    c.field(10, 1, "default")
    c.op("TEST", 10)
    c.jump("done")
    c.op("SETFIELD", 1, c.index("selected"), 10)
    c.jump("done")
    c.mark("scan")
    c.field(10, 1, "params")
    c.op("LEN", 11, 10)
    c.load(12, 1)
    c.mark("candidate")
    c.op("LE", 0, 12, 11)
    c.jump("done")
    lookup(c, 13, 10, 12)
    c.op("EQ", 0, 9, 13)
    c.jump("next")
    c.op("SETFIELD", 1, c.index("selected"), 12)
    c.jump("done")
    c.mark("next")
    c.op("ADD", 12, 12, 256 + c.index(1))
    c.jump("candidate")
    c.mark("done")


def refresh(c, base):
    c.glob(base, "provide_update")
    c.glob(base + 1, "m_needs_rebuild")
    c.op("LOADBOOL", base + 2, 0)
    c.wide("SETGLOBAL", base + 2, c.index("m_needs_rebuild"))
    c.op("CALL_I", base, 2, 1)


def callback(target):
    c = Code({"constants": []})
    c.op("EQ", 0, 0, 256 + c.index("CLICKED"))
    c.jump("end")
    c.op("EQ", 0, 2, 256 + c.index(0))
    c.jump("end")
    c.glob(4, OWNERS)
    c.op("TEST", 4)
    c.jump("end")
    c.op("MOVE", 5, 1)
    # The listener is on a button nested inside the row's visual group.
    # Resolve ownership from the clicked window even when another row has focus.
    c.load(7, 4)
    c.mark("owner")
    lookup(c, 6, 4, 5)
    c.op("TEST", 6, 0, 1)
    c.jump("found")
    c.op("SUB", 7, 7, 256 + c.index(1))
    c.op("EQ", 1, 7, 256 + c.index(0))
    c.jump("end")
    c.native(8, "get_parent_ui_win")
    c.op("MOVE", 9, 5)
    c.op("CALL_I", 8, 2, 2)
    c.op("TEST", 8)
    c.jump("end")
    c.op("MOVE", 5, 8)
    c.jump("owner")
    c.mark("found")
    c.glob(7, "m_cur_options")
    lookup(c, 7, 7, 6)
    c.op("TEST", 7)
    c.jump("end")
    c.field(8, 7, WIDGET)
    c.op("EQ", 0, 8, 256 + c.index("toggle" if target is None else "hud"))
    c.jump("end")
    c.glob(8, "set_selected_option")
    c.op("MOVE", 9, 6)
    c.op("CALL_I", 8, 2, 1)
    c.glob(8, "veil_set_options_focus")
    c.op("LOADBOOL", 9, 1)
    c.op("CALL_I", 8, 2, 1)
    c.glob(8, "change_internal_option")
    if target is None:
        c.load(9, 1)
    else:
        c.field(9, 7, "get")
        c.op("CALL_I", 9, 1, 2)
        c.op("TEST", 9)
        c.jump("end")
        c.field(9, 9, "param")
        c.field(10, 7, "params")
        c.op("LEN", 11, 10)
        c.op("LT", 1, 11, 256 + c.index(target))
        c.jump("end")
        c.load(12, 1)
        c.mark("locate")
        lookup(c, 13, 10, 12)
        c.op("EQ", 1, 13, 9)
        c.jump("located")
        c.op("ADD", 12, 12, 256 + c.index(1))
        c.op("LE", 1, 12, 11)
        c.jump("locate")
        c.jump("end")
        c.mark("located")
        c.load(10, target)
        c.op("SUB", 9, 10, 12)
        c.op("EQ", 0, 9, 256 + c.index(0))
        c.jump("change")
        # Accept an explicit re-selection of the current HUD size as a restore.
        c.field(10, 7, "id")
        c.op("EQ", 0, 10, 256 + c.index("huddisplay"))
        c.jump("update")
        c.field(10, 7, "callback")
        c.load(11, target)
        c.op("CALL_I", 10, 2, 1)
        c.jump("update")
        c.mark("change")
    c.op("CALL_I", 8, 2, 1)
    c.mark("update")
    refresh(c, 8)
    c.mark("end")
    c.op("RETURN", 0, 1)
    return c


def arrow_press(c):
    # A pointer arrow already identifies both the row and direction. Dispatch
    # its value operation directly, before controller focus can consume Left.
    c.op("EQ", 0, 0, 256 + c.index("DOWN"))
    c.jump("other")
    c.op("EQ", 0, 2, 256 + c.index(0))
    c.jump("other")
    c.glob(4, "m_arrows")
    c.op("TEST", 4)
    c.jump("other")
    c.op("SELF", 4, 4, 256 + c.index("get_button_id"))
    c.op("MOVE", 6, 1)
    c.op("CALL_I", 4, 3, 2)
    c.op("TEST", 4)
    c.jump("other")
    c.glob(5, "string")
    c.field(5, 5, "match")
    c.op("MOVE", 6, 4)
    c.load(7, "^(%d+)_(%a+)$")
    c.op("CALL_I", 5, 3, 3)
    c.op("TEST", 5)
    c.jump("other")
    c.load(7, -1)
    c.op("EQ", 1, 6, 256 + c.index("left"))
    c.jump("row")
    c.op("EQ", 0, 6, 256 + c.index("right"))
    c.jump("other")
    c.load(7, 1)
    c.mark("row")
    c.glob(8, "tonumber")
    c.op("MOVE", 9, 5)
    c.op("CALL_I", 8, 2, 2)
    c.glob(9, "m_cur_options")
    lookup(c, 9, 9, 8)
    c.op("TEST", 9)
    c.jump("other")
    c.glob(9, "set_selected_option")
    c.op("MOVE", 10, 8)
    c.op("CALL_I", 9, 2, 1)
    c.glob(9, "veil_set_options_focus")
    c.op("LOADBOOL", 10, 1)
    c.op("CALL_I", 9, 2, 1)
    c.glob(9, "change_internal_option")
    c.op("MOVE", 10, 7)
    c.op("CALL_I", 9, 2, 1)
    refresh(c, 9)
    c.op("RETURN", 0, 1)
    c.mark("other")
