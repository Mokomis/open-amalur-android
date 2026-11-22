"""Host checks for the thermal resolution floor and its hysteresis."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'native/android/app/oa_thermal.h'
HARNESS = r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "oa_thermal.h"
int main(int argc, char **argv) {
    /* argv: floor, then pairs of headroom (or "nan") and status; prints each floor. */
    unsigned floor = (unsigned)atoi(argv[1]);
    for (int i = 2; i + 1 < argc; i += 2) {
        float headroom = argv[i][0] == 'n' ? NAN : strtof(argv[i], NULL);
        floor = oa_thermal_floor(headroom, atoi(argv[i + 1]), floor);
        printf("%u ", floor);
    }
    return 0;
}
'''


class ThermalFloor(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        harness = Path(cls.folder.name) / 'check.c'
        harness.write_text(HARNESS)
        cls.exe = Path(cls.folder.name) / 'check'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-I', str(HEADER.parent), str(harness),
                        '-o', str(cls.exe), '-lm'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def floors(self, start, *readings):
        args = [str(start)] + [str(value) for reading in readings for value in reading]
        return [int(v) for v in subprocess.check_output([self.exe, *args], text=True).split()]

    def test_cool_device_keeps_full_resolution(self):
        self.assertEqual(self.floors(0, (0.3, 0), (0.6, 1), ('nan', 0)), [0, 0, 0])

    def test_headroom_raises_floor_immediately(self):
        self.assertEqual(self.floors(0, (0.86, 0), (0.96, 0)), [1, 2])
        self.assertEqual(self.floors(0, (0.97, 0)), [2])

    def test_status_alone_raises_floor_without_headroom(self):
        self.assertEqual(self.floors(0, ('nan', 2), ('nan', 3)), [1, 2])

    def test_floor_falls_one_step_after_cooling(self):
        # From low: stays low until below 0.85, then medium until below 0.75, then full.
        self.assertEqual(self.floors(2, (0.9, 0), (0.8, 0), (0.8, 0), (0.7, 0)), [2, 1, 1, 0])

    def test_status_holds_floor_until_it_drops(self):
        self.assertEqual(self.floors(2, ('nan', 3), ('nan', 2), ('nan', 1), ('nan', 0)), [2, 1, 0, 0])

    def test_hot_status_overrides_cool_headroom(self):
        self.assertEqual(self.floors(0, (0.2, 3)), [2])


if __name__ == '__main__':
    unittest.main()
