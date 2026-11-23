from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class AndroidTouchTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "Host JDK unavailable")
    def test_hotbar_matches_visible_buttons(self):
        with tempfile.TemporaryDirectory() as folder:
            subprocess.run(["javac", "--release", "8", "-d", folder,
                            str(ROOT / "android/src/TouchHotbar.java"),
                            str(Path(__file__).with_name("TouchHotbarTest.java"))], check=True)
            subprocess.run(["java", "-cp", folder,
                            "org.openamalur.originalengine.TouchHotbarTest"], check=True)

    def test_lock_controls_and_camera_profile_across_launches(self):
        java = shutil.which("java")
        javac = shutil.which("javac")
        if not javac or not java:
            self.skipTest("Host JDK unavailable")
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            # RunLogs only calls the Android logger on a failed directory prune.
            log = out / "Log.java"
            log.write_text("package android.util; public class Log { "
                           "public static int w(String t,String m,Throwable e){return 0;} }")
            source = ROOT / "android/src"
            subprocess.run([javac, "--release", "8", "-d", folder, str(log),
                            str(source / "RunLogs.java"), str(source / "PersonalSettings.java"),
                            str(source / "LockTouch.java"), str(Path(__file__).with_name("TouchIssue17Test.java"))],
                           check=True)
            for action in ("fresh", "upgrade", "relaunch"):
                storage = out / ("fresh" if action == "fresh" else "existing")
                storage.mkdir(exist_ok=True)
                subprocess.run([java, "-cp", folder, "org.openamalur.originalengine.TouchIssue17Test",
                                str(storage), action], check=True)
