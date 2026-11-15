"""Apply Veil entry points to the linked ARM64 library after checking its instructions."""

import struct
import subprocess


def executable_offset(data, address):
    headers = struct.unpack_from("<Q", data, 32)[0]
    size, count = struct.unpack_from("<HH", data, 54)
    for index in range(count):
        kind, flags, offset, va, _, length = struct.unpack_from("<IIQQQQ", data, headers + size * index)
        if kind == 1 and flags & 1 and va <= address < va + length:
            return offset + address - va
    raise ValueError(f"No executable segment at {address:#x}")


def replace_instruction(data, address, expected, new):
    offset = executable_offset(data, address)
    actual = struct.unpack_from("<I", data, offset)[0]
    assert actual == expected, (hex(address), hex(actual), hex(expected))
    struct.pack_into("<I", data, offset, new)


def branch(address, target, link=False):
    displacement = target - address
    assert displacement % 4 == 0 and -(1 << 27) <= displacement < 1 << 27
    return (0x94000000 if link else 0x14000000) | ((displacement // 4) & 0x3ffffff)



def install_veil_hooks(library, clang, assembly, directory):
    nm = clang.with_name("llvm-nm.exe")
    objcopy = clang.with_name("llvm-objcopy.exe")
    symbols = {parts[2]: int(parts[0], 16)
               for line in subprocess.run([nm, "--defined-only", library], capture_output=True, text=True, check=True).stdout.splitlines()
               if len(parts := line.split()) == 3 and parts[1].lower() in ("t", "d", "b")}
    data = bytearray(library.read_bytes())
    for label, guest, first in (
        ("pad_get", "host_pad_get", 0xa9bd7bfd),
        ("read_save", "offline_read", 0xa9bd7bfd),
        ("write_save", "offline_write", 0x52a08008),
        ("skin", "f_00F7CFA0", 0xd10343ff),
        ("component", "f_00F24EF0", 0xa9bd7bfd),
        ("portrait_lookup", "f_01025FA0", 0xa9bd7bfd),
        ("portrait_model", "f_00F046F0", 0xa9be7bfd),
    ):
        entry = symbols[guest]
        trampoline = symbols["veil_stock_" + label]
        assert struct.unpack_from("<I", data, executable_offset(data, trampoline))[0] == first
        replace_instruction(data, entry, first, branch(entry, symbols["veil_trace_" + label]))
        replace_instruction(data, trampoline + 4, 0x14000000, branch(trampoline + 4, entry + 4))

    for source, offset, old_target, new_target in (
        ("f_00F7CFA0", 0x2fc, "f_00F5C2A0", "veil_preview_style_effects"),
        ("f_00F7CFA0", 0x1160, "f_00F74BD0", "veil_preview_held_item"),
        ("f_00D13ED0", 0x128, "f_00C43700", "veil_shadow_prepare_cascades"),
        ("f_00D32B10", 0x245c, "f_00D13ED0", "veil_shadow_build_cascades"),
        ("f_00D42420", 0x10c, "f_00CA0AA0", "veil_vegetation_select_cells"),
        ("f_00C982F0", 0x1270, "f_00C74280", "veil_occlusion_render"),
        ("f_00D11020", 0xbc0, "f_00C6F160", "veil_depth_of_field_render"),
        ("f_00D11020", 0x1180, "f_00C6F160", "veil_depth_of_field_render"),
    ):
        location = symbols[source] + offset
        replace_instruction(data, location, branch(location, symbols[old_target], True),
                            branch(location, symbols[new_target], True))
    location = symbols["f_00C58ED0"] + 0x210
    replace_instruction(data, location, 0x52808000, branch(location, symbols["veil_shadow_map_dimension"], True))
    location = symbols["f_00CA0AA0"] + 0x484
    replace_instruction(data, location, 0x1e270162, branch(location, symbols["veil_vegetation_radius"], True))
    location = symbols["f_00C982F0"] + 0x13c
    replace_instruction(data, location, 0x08dffe30, branch(location, symbols["veil_occlusion_gate"], True))
    location = symbols["f_00C6F160"] + 0x12c0
    replace_instruction(data, location, 0x9e6701e2, branch(location, symbols["veil_depth_of_field_radius"], True))

    # Restore Veil's established direct-to-menu and startup input patches.
    intro = symbols["f_00A65A60"]
    replace_instruction(data, intro + 0x90, 0x540001A3, 0xD503201F)
    replace_instruction(data, intro + 0xc0, 0x54FFFEA2, 0x17FFFFF5)
    startup = directory / "startup_input.o"
    raw = directory / "startup_input.bin"
    subprocess.run([clang, "--target=aarch64-linux-android29", "-c", assembly, "-o", startup], check=True)
    subprocess.run([objcopy, "--only-section=.text", "-O", "binary", startup, raw], check=True)
    adapter = intro + 0x12c
    body = raw.read_bytes()
    assert len(body) == 0x90 and body[-4:] == struct.pack("<I", 0x14000000)
    offset = executable_offset(data, adapter)
    assert struct.unpack_from("<I", data, offset)[0] == 0xF9400291
    acquisition = symbols["f_01076440"]
    data[offset:offset + len(body)] = body[:-4] + struct.pack(
        "<I", branch(adapter + len(body) - 4, acquisition))
    location = symbols["f_00EFAE20"] + 0xb0
    replace_instruction(data, location, branch(location, acquisition, True),
                        branch(location, adapter, True))
    library.write_bytes(data)
