"""Show the latest save beneath the Home menu when Continue is highlighted."""

from fit_veil_panels import Layout, LEFT, RIGHT, TOP, BOTTOM, WIDTH, HEIGHT
from mark_veil_portraits import Code, insert
from amalur_script import parse_bytes
from patch_veil_environment import repack_script


def patch_home(data):
    parsed = parse_bytes(data)
    children = parsed["chunk"]["root_function"]["children"]
    builder, frame = children[17], children[58]
    c = Code(builder)
    c.glob(6, "m_most_recent_save_slot")
    c.op("ADD", 6, 6, 256 + c.index(1))
    c.load(7, "")
    c.wide("SETGLOBAL", 7, c.index("m_veil_continue_preview"))
    for method, pos in [
        ("get_save_slot_player_name", 7),
        ("get_save_slot_player_level", 9),
    ]:
        c.glob(pos, "SAVE_RESTORE")
        c.field(pos, pos, method)
        c.op("CALL_I", pos, 1, 2)
        c.op("TEST", pos)
        c.jump("done")
        c.op("GETTABLE_S", pos, pos, 6)
        c.op("DATA")
        c.op("DATA")
        c.op("TEST", pos)
        c.jump("done")
    c.glob(12, "type")
    c.op("MOVE", 13, 7)
    c.op("CALL_I", 12, 2, 2)
    c.op("EQ", 0, 12, 256 + c.index("string"))
    c.jump("done")
    c.op("EQ", 1, 7, 256 + c.index(""))
    c.jump("done")
    c.glob(12, "type")
    c.op("MOVE", 13, 9)
    c.op("CALL_I", 12, 2, 2)
    c.op("EQ", 0, 12, 256 + c.index("number"))
    c.jump("done")
    c.load(8, "   |   Level ")
    c.op("CONCAT", 7, 7, 9)
    c.wide("SETGLOBAL", 7, c.index("m_veil_continue_preview"))
    c.mark("done")
    a, b, p = insert(data, builder, 68, c, 16)
    s = Code(frame)
    s.glob(6, "m_veil_continue_preview")
    s.op("TEST", 6)
    s.jump("done")
    s.glob(7, "m_button_tree")
    s.op("SELF", 7, 7, 256 + s.index("is_root"))
    s.op("CALL_I", 7, 2, 2)
    s.op("TEST", 7)
    s.jump("done")
    s.glob(7, "m_button_tree")
    s.op("SELF", 7, 7, 256 + s.index("get_highlight_node"))
    s.op("CALL_I", 7, 2, 2)
    s.op("TEST", 7)
    s.jump("done")
    s.field(7, 7, "id_string")
    s.op("EQ", 0, 7, 256 + s.index(3004181))
    s.jump("done")
    s.op("MOVE", 4, 6)
    s.mark("done")
    x, y, z = insert(data, frame, 110, s, 12)
    button = children[46]
    hit = Code(button)
    hit.op("EQ", 0, 2, 256 + hit.index(0))
    hit.jump("done")
    hit.glob(8, "m_button_tree")
    hit.op("SELF", 8, 8, 256 + hit.index("handle_mouseover"))
    hit.op("MOVE", 10, 1)
    hit.op("LOADBOOL", 11, 1)
    hit.op("CALL_I", 8, 4, 1)
    hit.mark("done")
    h, i, t = insert(data, button, 17, hit, 12)
    return repack_script(
        data, parsed, data[:a] + p + data[b:h] + t + data[i:x] + z + data[y:]
    )


def home_layout(data):
    layout = Layout(data)
    fields = {
        layout.symbols[w & 4095]: layout.values[(w >> 12) & 4095]
        for w in layout.attributes[188]
    }
    if fields.get(3971938) != b"motd_window":
        raise ValueError("Unrecognized title layout")
    # Align the latest save with Continue across both 16:9 and 3:2 displays.
    layout.set(
        188,
        {
            8368203: 418,
            7030313: 348,
            29510429: None,
            921821: None,
            LEFT: None,
            RIGHT: None,
            TOP: None,
            BOTTOM: None,
            WIDTH: 550,
            HEIGHT: 35,
        },
    )
    return layout.encode()
