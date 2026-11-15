"""Compile the project's shaders into C headers for the Android renderer."""

from pathlib import Path
import struct
import subprocess

SHADERS = {
    "oa_hdr_shaders.h": (
        ("fullscreen.vert", "oa_hdr_vertex"),
        ("highlights.frag", "oa_hdr_merge_fragment"),
        ("hdr_present.frag", "oa_hdr_output_fragment"),
        ("night_grade.frag", "oa_night_grade_fragment"),
    ),
    "oa_pause_spv.h": (
        ("pause.vert", "oa_pause_vertex"),
        ("pause.frag", "oa_pause_fragment"),
    ),
    "waterfall_composite.h": (
        ("waterfall_composite.frag", "waterfall_fragment"),
    ),
}


def compile_shaders(sources, ndk, output):
    tools = ndk / "shader-tools/windows-x86_64"
    output.mkdir()
    for header, shaders in SHADERS.items():
        content = []
        for filename, symbol in shaders:
            spirv = output / (filename + ".spv")
            subprocess.run([tools / "glslc.exe", "--target-env=vulkan1.3", "-O",
                            sources / filename, "-o", spirv], check=True)
            validator = tools / "spirv-val.exe"
            if validator.is_file():
                subprocess.run([validator, "--target-env", "vulkan1.3", spirv], check=True)
            data = spirv.read_bytes()
            words = struct.unpack(f"<{len(data) // 4}I", data)
            content.append(f"static const uint32_t {symbol}[] = {{")
            for offset in range(0, len(words), 8):
                content.append("    " + ", ".join(f"0x{word:08x}" for word in words[offset:offset + 8]) + ",")
            content.append("};")
        (output / header).write_text("\n".join(content) + "\n", encoding="utf-8")
