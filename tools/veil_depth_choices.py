"""Build the shared Options layout and adapt ordered settings to native sliders."""

from fit_veil_panels import Layout, HEIGHT, LEFT, TOP, WIDTH
from mark_veil_portraits import Code
from veil_choice_widgets import ID, TEXT, CALLBACK, lookup
from veil_gameplay_choices import add_templates

# A known binary UI symbol outside the options layout. Both the XML reader and
# the script VM can resolve this ID on every supported game build.
NAV_HINT_NAME, NAV_HINT_ID = "new_cinematic", 10834273
CATEGORY_HINT = "Up/Down   Browse\nRight / A   Open"


def layout_with_choices(data):
    layout = Layout(data)

    def attr(node, key):
        return next(
            layout.values[(w >> 12) & 4095]
            for w in layout.attributes[node]
            if layout.symbols[w & 4095] == key
        )

    if (
        attr(81, ID) != b"slider_template"
        or attr(270, CALLBACK) != b"on_veil_category_1"
    ):
        raise ValueError("Unexpected Veil slider or button template")

    def append(parent, model):
        idx = len(layout.nodes)
        layout.nodes.append([parent, -1, -1, 0, 0])
        layout.attributes.append(layout.attributes[model][:])
        last = layout.nodes[parent][1]
        if last < 0:
            layout.nodes[parent][1] = idx
        else:
            while layout.nodes[last][2] >= 0:
                last = layout.nodes[last][2]
            layout.nodes[last][2] = idx
        return idx

    def clone(parent, model, mapping):
        node = append(parent, model)
        mapping[model] = node
        child = layout.nodes[model][1]
        while child >= 0:
            clone(node, child, mapping)
            child = layout.nodes[child][2]
        return node

    hint = b"LB / RB   Category"
    if layout.values.count(hint) != 1:
        raise ValueError("Unrecognized category hint")
    (hint_node,) = (
        i
        for i, words in enumerate(layout.attributes)
        if any(
            layout.symbols[w & 4095] == TEXT and layout.values[(w >> 12) & 4095] == hint
            for w in words
        )
    )
    layout.set(
        hint_node, {ID: NAV_HINT_NAME, TEXT: CATEGORY_HINT, HEIGHT: 100, 5487086: "Top"}
    )
    # The note is outside the scrolling rows and shares the footer with Apply.
    footer = clone(117, 280, {})
    layout.set(
        footer,
        {ID: "motd_label", LEFT: 48, TOP: 640, WIDTH: 490, HEIGHT: 72, TEXT: " "},
    )
    add_templates(layout, append, clone)
    return layout.encode()


# These options expose ordered engine values through a one-based choice callback.
# Keep their original parameters and callbacks. Only the slider getter needs
# to translate the current parameter into its position in that list.
ORDERED = ("supersampling", "veil_render_resolution", "shadow")
BOUNDING = "veil_camera_bounding"


def adapter_name(setting):
    return "veil_slider_index_" + setting


def adapter_source(setting):
    return "m_veil_source_" + setting


def choice_index(setting):
    c = Code({"constants": []})
    c.glob(0, adapter_source(setting))
    c.field(1, 0, "veil_original_get")
    c.op("CALL_I", 1, 1, 2)
    c.op("TEST", 1)
    c.jump("fallback")
    c.field(1, 1, "param")
    c.field(2, 0, "params")
    c.op("LEN", 3, 2)
    c.load(4, 1)
    c.mark("scan")
    c.op("LE", 0, 4, 3)
    c.jump("fallback")
    lookup(c, 5, 2, 4)
    c.op("EQ", 0, 1, 5)
    c.jump("next")
    c.op("RETURN", 4, 2)
    c.mark("next")
    c.op("ADD", 4, 4, 256 + c.index(1))
    c.jump("scan")
    c.mark("fallback")
    c.load(0, 1)
    c.op("RETURN", 0, 2)
    return c


def bounding_getter():
    c = Code({"constants": []})
    c.glob(0, adapter_source(BOUNDING))
    c.field(1, 0, "veil_original_get")
    c.op("CALL_I", 1, 1, 2)
    c.op("NEWTABLE", 2, 0, 2)
    c.op("SETFIELD", 2, c.index("param"), 1)
    c.op("ADD", 1, 1, 256 + c.index(1))
    c.field(3, 0, "display_vals")
    lookup(c, 3, 3, 1)
    c.op("SETFIELD", 2, c.index("display"), 3)
    c.op("RETURN", 2, 2)
    return c


def bounding_setter():
    c = Code({"constants": []})
    c.glob(1, adapter_source(BOUNDING))
    c.field(1, 1, "veil_original_set")
    c.op("SUB", 2, 0, 256 + c.index(1))
    c.op("CALL_I", 1, 2, 1)
    c.op("RETURN", 0, 1)
    return c


def row_adapter():
    c = Code({"constants": []})
    c.op("MOVE", 8, 0)
    # The list builder holds the real option in R8 before choosing its template.
    c.field(9, 8, "veil_original_get")
    c.op("TEST", 9, 0, 1)
    c.jump("done")
    c.field(9, 8, "id")
    for name in (*ORDERED, BOUNDING):
        c.op("EQ", 0, 9, 256 + c.index(name))
        c.jump("next_" + name)
        c.wide("SETGLOBAL", 8, c.index(adapter_source(name)))
        c.field(10, 8, "get")
        c.op("SETFIELD", 8, c.index("veil_original_get"), 10)
        c.glob(10, adapter_name(name))
        c.op("SETFIELD", 8, c.index("get"), 10)
        if name == BOUNDING:
            c.op("LOADNIL", 10, 10)
            c.op("SETFIELD", 8, c.index("int_range"), 10)
            c.field(10, 8, "callback")
            c.op("SETFIELD", 8, c.index("veil_original_set"), 10)
            c.glob(10, "veil_set_camera_bounding")
            c.op("SETFIELD", 8, c.index("callback"), 10)
            c.op("NEWTABLE", 10, 2)
            c.load(11, 0)
            c.load(12, 1)
            c.op("SETLIST", 10, 2, 1)
            c.op("SETFIELD", 8, c.index("params"), 10)
        else:
            c.field(12, 8, "params")
            c.op("LEN", 12, 12)
            c.load(11, 1)
            c.op("NEWTABLE", 10, 2)
            c.op("SETLIST", 10, 2, 1)
            c.op("SETFIELD", 8, c.index("int_range"), 10)
        if name in (BOUNDING, "veil_render_resolution"):
            c.load(10, 1)
            c.op("SETFIELD", 8, c.index("default"), 10)
        c.jump("done")
        c.mark("next_" + name)
    c.mark("done")
    c.op("RETURN", 0, 1)
    return c


def prepare_row(c):
    c.glob(9, "veil_adapt_options_row")
    c.op("MOVE", 10, 8)
    c.op("CALL_I", 9, 2, 1)


def restart_footer(c):
    c.native(11, "find_static_type")
    c.glob(12, "m_window")
    c.load(13, 1221325)  # Text label on the background panel
    c.load(14, 6005930)  # motd_label
    c.load(15, -1)
    c.op("LOADBOOL", 16, 1)
    c.op("CALL_I", 11, 6, 2)
    c.op("TEST", 11)
    c.jump("footer_done")
    c.native(12, "set_label_text")
    c.op("MOVE", 13, 11)
    c.load(14, " ")
    c.op("EQ", 0, 0, 256 + c.index("Graphics"))
    c.jump("footer_paint")
    c.load(
        14, "Restart the game to apply changes to\nAmbient Occlusion or Depth of Field."
    )
    c.mark("footer_paint")
    c.op("CALL_I", 12, 3, 1)
    c.mark("footer_done")


def suppress_inline_restart(c):
    c.field(14, 4, "id")
    c.op("EQ", 1, 14, 256 + c.index("veil_ambient_occlusion"))
    c.jump("footer")
    c.op("EQ", 0, 14, 256 + c.index("veil_depth_of_field"))
    c.jump("done")
    c.mark("footer")
    c.load(8, " ")
    c.mark("done")


def numeric_label(c):
    # Native Veil sliders already paint their own localized value and restart note.
    c.field(14, 4, "veil_active")
    c.op("TEST", 14, 0, 1)
    c.jump("done")
    c.field(15, 4, "get")
    c.op("CALL_I", 15, 1, 2)
    c.field(14, 4, "display_vals")
    c.op("TEST", 14)
    c.jump("number")
    lookup(c, 15, 14, 15)
    c.jump("paint")
    c.mark("number")
    c.glob(14, "tostring")
    c.op("CALL_I", 14, 2, 2)
    c.op("MOVE", 15, 14)
    c.mark("paint")
    c.native(16, "find_static")
    c.op("MOVE", 17, 5)
    c.load(18, 17695542)
    c.load(19, -1)
    c.op("LOADBOOL", 20, 1)
    c.op("CALL_I", 16, 5, 2)
    c.native(17, "set_label_text")
    c.op("MOVE", 18, 16)
    c.op("MOVE", 19, 15)
    c.op("CALL_I", 17, 3, 1)
    c.mark("done")
