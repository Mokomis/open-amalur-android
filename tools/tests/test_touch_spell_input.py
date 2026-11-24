"""Exercise touch-to-controller spell handoff using the production queue."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "native/android/app"


class TouchSpellInput(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix" and shutil.which("cc"), "POSIX C compiler unavailable")
    def test_cast_handoff(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "touch-spell-input"
            subprocess.run(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                            "-I", str(ROOT / "native/runtime"), "-I", str(APP), "-I", str(APP / "release"),
                            str(Path(__file__).with_name("touch_spell_input_test.c")), "-pthread",
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
