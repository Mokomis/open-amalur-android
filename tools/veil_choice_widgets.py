"""Shared Veil choice-widget identifiers and script operations."""

ID, TEXT, CALLBACK = 3971938, 900629, 13407116
# Engine symbols shared by the layout reader and the script VM. These identifiers
# are unique within the settings window, including the six sidebar highlights.
MARKS = (
    ("motd_label", 6005930),
    ("title_logo_window", 16325262),
    ("title_logo_window_fatesworn", 46657361),
    ("slider_template", 12804700),
)


def visibility(code, handle, value, base):
    code.native(base, "set_visible")
    code.op("MOVE", base + 1, handle)
    code.op("MOVE", base + 2, value)
    code.op("CALL_I", base, 3, 1)


def lookup(c, dest, table, key):
    c.op("GETTABLE_S", dest, table, key)
    c.op("DATA")
    c.op("DATA")
