"""Smoke-test Open Amalur on a connected Android device over adb.

    python tools/device_smoke_test.py [--apk open-amalur-android-X.apk] [--serial SERIAL]

Needs a device that already has Open Amalur set up with game files. With --apk
the build is installed as an update (adb install -r), which keeps saves and
settings; the script never uninstalls. It then checks that:

  - saves are still present after the install
  - the game starts and reaches the game screen without crashing
  - old run logs are pruned
  - switching dark mode and font size does not restart the game
  - tapping the launcher icon again returns to the same game screen

The device's dark mode and font size are restored afterwards. A screenshot is
saved next to the script's output. Exit status is 0 only if every check passed.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import time

PACKAGE = "org.openamalur.originalengine"
FILES = f"/sdcard/Android/data/{PACKAGE}/files"
SAVES = f"{FILES}/offline-saves/working/autocloud/save"
RUN_LOGS_KEPT = 10


class Device:
    def __init__(self, serial):
        self.prefix = ["adb"] + (["-s", serial] if serial else [])

    def run(self, *args, check=True, timeout=300):
        result = subprocess.run(self.prefix + list(args), capture_output=True, text=True, timeout=timeout)
        if check and result.returncode:
            raise RuntimeError(f"adb {' '.join(args)} failed: {result.stderr.strip() or result.stdout.strip()}")
        return result.stdout

    def shell(self, command, check=False):
        return self.run("shell", command, check=check).strip()

    def pid(self):
        return self.shell(f"pidof {PACKAGE}")

    def game_screens(self):
        records = re.findall(rf"\* ActivityRecord\{{(\w+) [^}}]*{re.escape(PACKAGE)}/\.VeilActivity",
                             self.shell("dumpsys activity activities"))
        return len(set(records))

    def resumed(self):
        line = next((l for l in self.shell("dumpsys activity activities").splitlines()
                     if "topResumedActivity" in l), "")
        return f"{PACKAGE}/.VeilActivity" in line

    def launch(self):
        self.shell(f"monkey -p {PACKAGE} -c android.intent.category.LAUNCHER 1")


class Report:
    def __init__(self):
        self.failures = 0

    def check(self, name, passed, detail=""):
        print(f"{'PASS' if passed else 'FAIL'}  {name}" + (f"  ({detail})" if detail else ""), flush=True)
        self.failures += not passed
        return passed


def wait_for(condition, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if condition():
            return True
        time.sleep(1)
    return condition()


def crash_lines(device):
    log = device.run("logcat", "-d", "-v", "brief", check=False)
    patterns = (r"FATAL EXCEPTION", rf"Process: {re.escape(PACKAGE)}", r"OpenAmalur.*FATAL:", r"I/OpenAmalur.*FATAL")
    return [line for line in log.splitlines() if any(re.search(p, line) for p in patterns)]


def main():
    cli = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    cli.add_argument("--apk", type=Path, help="install this APK as an update first")
    cli.add_argument("--serial", help="adb device serial (default: the only connected device)")
    cli.add_argument("--start-timeout", type=int, default=90, help="seconds to wait for the game screen")
    cli.add_argument("--screenshot", type=Path, default=Path("device-smoke-test.png"))
    args = cli.parse_args()
    device = Device(args.serial)
    report = Report()

    model = device.shell("getprop ro.product.model")
    if not model:
        raise SystemExit("No device found. Connect one with USB or wireless debugging and try again.")
    version = re.search(r"versionName=(\S+)", device.shell(f"dumpsys package {PACKAGE}"))
    if not version:
        raise SystemExit(f"{PACKAGE} is not installed on {model}. Set up Open Amalur on the device first.")
    print(f"Device: {model}, Android {device.shell('getprop ro.build.version.release')}, "
          f"Open Amalur {version[1]}")

    saves_before = device.shell(f"ls {SAVES} 2>/dev/null | wc -l")
    if args.apk:
        output = device.run("install", "-r", str(args.apk), check=False, timeout=600)
        report.check("install as update", "Success" in output, output.strip().splitlines()[-1] if output.strip() else "")
        version = re.search(r"versionName=(\S+)", device.shell(f"dumpsys package {PACKAGE}"))
        print(f"Installed version: {version[1] if version else 'unknown'}")
    report.check("saves kept", device.shell(f"ls {SAVES} 2>/dev/null | wc -l") == saves_before,
                 f"{saves_before} save files")

    # Fresh start.
    device.shell(f"am force-stop {PACKAGE}")
    device.run("logcat", "-c", check=False)
    device.launch()
    started = wait_for(lambda: device.resumed() and device.pid(), args.start_timeout)
    report.check("game screen reached", started)
    time.sleep(5)
    pid = device.pid()
    crashes = crash_lines(device)
    report.check("no crash at startup", not crashes, crashes[0] if crashes else "")
    runs = device.shell(f"ls -d {FILES}/run-* 2>/dev/null | wc -l")
    report.check("run logs pruned", runs.isdigit() and int(runs) <= RUN_LOGS_KEPT + 1, f"{runs} run folders")

    # Configuration changes must not restart the game screen.
    night = device.shell("cmd uimode night")
    font = device.shell("settings get system font_scale") or "1.0"
    try:
        flipped = "no" if "yes" in night else "yes"
        device.shell(f"cmd uimode night {flipped}")
        time.sleep(6)
        device.shell("settings put system font_scale 1.15")
        time.sleep(6)
        report.check("dark mode and font size keep the game running",
                     device.pid() == pid and device.game_screens() == 1,
                     f"pid {pid} -> {device.pid()}, {device.game_screens()} game screen(s)")
    finally:
        restore = "yes" if "yes" in night else "auto" if "auto" in night else "no"
        device.shell(f"cmd uimode night {restore}")
        device.shell(f"settings put system font_scale {font if font != 'null' else '1.0'}")
        time.sleep(4)

    # Home, then the launcher icon, must return to the same game screen.
    device.shell("input keyevent KEYCODE_HOME")
    time.sleep(4)
    device.launch()
    returned = wait_for(device.resumed, 20)
    report.check("launcher icon returns to the running game",
                 returned and device.pid() == pid and device.game_screens() == 1,
                 f"{device.game_screens()} game screen(s)")

    crashes = crash_lines(device)
    report.check("no crash during the test", not crashes, crashes[0] if crashes else "")
    with args.screenshot.open("wb") as image:
        image.write(subprocess.run(device.prefix + ["exec-out", "screencap", "-p"], capture_output=True).stdout)
    print(f"Screenshot: {args.screenshot}")
    print("All checks passed." if not report.failures else f"{report.failures} check(s) failed.")
    return 1 if report.failures else 0


if __name__ == "__main__":
    sys.exit(main())
