"""Exercise original sprint / context ownership and intentional two-finger input."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TouchRunInteraction(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix" and shutil.which("cc"), "POSIX C compiler unavailable")
    def test_context(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "touch-run-context"
            command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-I", str(ROOT / "native"), "-I", str(ROOT / "native/runtime"),
                       "-I", str(ROOT / "native/android/app"),
                       "-I", str(ROOT / "native/android/app/release")]
            # A prepared commercial-input shard is optional. Run the identical
            # fixture through the full original matching loop when it is available.
            shard = os.environ.get("OA_TOUCH_RUN_PREPARED_SHARD")
            if shard:
                source = Path(shard).read_text()
                start = source.index("void f_00BB39A0(X86Ctx *X86_RESTRICT ctx)\n{")
                stop = source.index("\n}", start) + 2
                self.assertIn("veil_drop_run_interaction", source[start:stop])
                (Path(folder) / "retail_match.inc").write_text(source[start:stop])
                command.extend(["-DRETAIL_MATCH", "-Wno-unused-label", "-I", folder])
            subprocess.run([*command, str(Path(__file__).with_name("touch_run_interaction_test.c")),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
