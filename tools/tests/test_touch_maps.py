import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class TouchMapTests(unittest.TestCase):
    def test_tap_drag_hold_and_pinch(self):
        source = Path(__file__).with_name("touch_map_gesture_test.c")
        with tempfile.TemporaryDirectory() as out:
            binary = Path(out) / "gesture"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
