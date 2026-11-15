"""Package a locally built library into an Android APK with optional Veil menus."""

import argparse
import os
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "org.openamalur.originalengine"


def run(*argv):
    subprocess.run([str(value) for value in argv], check=True)


def build(args):
    os.environ["JAVA_HOME"] = str(args.jdk.resolve())
    tools = args.sdk / "build-tools/36.0.0"
    android = args.sdk / "platforms/android-35/android.jar"
    output = args.output.resolve()
    if output.exists():
        raise FileExistsError(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory() as temporary:
        work = Path(temporary)
        manifest = (ROOT / "android/AndroidManifest.xml").read_text(encoding="utf-8")
        manifest = manifest.replace(f'package="{PACKAGE}"', f'package="{args.application_id}"')
        for activity in ("VeilActivity", "SetupActivity"):
            manifest = manifest.replace(f'android:name=".{activity}"', f'android:name="{PACKAGE}.{activity}"')
        template = work / "AndroidManifest.xml"
        template.write_text(manifest, encoding="utf-8")
        base = work / "manifest.apk"
        run(tools / "aapt2.exe", "link", "-I", android, "--manifest", template, "-o", base)

        classes = work / "classes"
        classes.mkdir()
        run(args.jdk / "bin/javac.exe", "--release", "8", "-classpath", android,
            "-d", classes, *sorted((ROOT / "android/src").glob("*.java")))
        # Collect classes in a ZIP so D8 gets one input across all Windows argument limits.
        jar = work / "classes.jar"
        with ZipFile(jar, "w", ZIP_DEFLATED) as output_classes:
            for item in sorted(classes.rglob("*.class")):
                output_classes.write(item, item.relative_to(classes).as_posix())
        run(tools / "d8.bat", "--min-api", "29", "--lib", android, "--output", work, jar)

        unsigned = work / "unsigned.apk"
        with ZipFile(base) as compiled, ZipFile(unsigned, "w", ZIP_DEFLATED) as apk:
            apk.writestr("AndroidManifest.xml", compiled.read("AndroidManifest.xml"))
            apk.write(work / "classes.dex", "classes.dex")
            apk.write(args.library, "lib/arm64-v8a/liboaoriginal.so", compress_type=ZIP_STORED)
            if args.veil_edits is not None:
                apk.write(args.veil_edits, "assets/local-build/veil-edits.bin")
                apk.write(args.veil_properties, "assets/local-build/veil-edits.properties")
            apk.write(ROOT / "LICENSE", "assets/licenses/Open-Amalur.txt")
            apk.write(ROOT / "THIRD_PARTY_NOTICES.md", "assets/licenses/Third-party-notices.md")
            apk.write(ROOT / "native/third-party/FAudio-0c423758/LICENSE", "assets/licenses/FAudio.txt")
            apache = ROOT / "native/dependencies/vulkan/LICENSE-Apache-2.0.txt"
            apk.write(apache, "assets/licenses/Vulkan-Headers.txt")
            for notice in sorted((ROOT / "android/licenses").glob("*.txt")):
                apk.write(notice, "assets/licenses/" + notice.name)
            for flag, name in (("waterfall", "veil-waterfall.bc3"), ("prototype_ui", "oa_prototype_ui.bin")):
                value = getattr(args, flag)
                if value is not None:
                    apk.write(value, "assets/" + name, compress_type=ZIP_STORED)

        aligned = work / "aligned.apk"
        run(tools / "zipalign.exe", "-f", "-P", "16", "4", unsigned, aligned)
        credential = ["--ks-pass", "env:" + args.password_env] if args.password_env else []
        run(tools / "apksigner.bat", "sign", "--ks", args.keystore, "--ks-key-alias",
            args.key_alias, *credential, "--out", output, aligned)
        run(tools / "apksigner.bat", "verify", output)
        run(tools / "zipalign.exe", "-c", "-P", "16", "4", output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("sdk", "jdk", "library", "keystore", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--veil-edits", type=Path)
    parser.add_argument("--veil-properties", type=Path)
    parser.add_argument("--key-alias", required=True)
    parser.add_argument("--password-env", help="name of an environment variable with the keystore password")
    parser.add_argument("--application-id", default=PACKAGE)
    parser.add_argument("--waterfall", type=Path)
    parser.add_argument("--prototype-ui", type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z][\w]*(?:\.[A-Za-z][\w]*)+", args.application_id, re.ASCII):
        parser.error("invalid Android application ID")
    if (args.veil_edits is None) != (args.veil_properties is None):
        parser.error("provide both Veil files or omit both for original menus")
    build(args)


if __name__ == "__main__":
    main()
