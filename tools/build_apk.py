"""Build the Android launcher or create a personal APK with a local engine."""

import argparse
from hashlib import file_digest, sha256
import json
import os
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "org.openamalur.originalengine"
ENGINE_RELEASES = "https://github.com/Mokomis/open-amalur-android/releases/download"
WATERFALL_BYTES = 256 * 320 * 100
RETROID_DRIVER = "DD-Turnip-v0.8.0"
RETROID_DRIVER_SHA256 = "e2bf336c2a10c47bd15ed581ee4ad99f2ad6b21c3b1f05af98d414faa0fdc043"


def run(*argv):
    subprocess.run([str(value) for value in argv], check=True)


def add_retroid_driver(apk, bundle, loader_build, loader_source):
    with ZipFile(bundle) as release:
        manifest = json.loads(release.read("manifest.json"))
        android = manifest["drivers"]["android"]
        driver = release.read(android["library"])
        if (manifest["tag"] != RETROID_DRIVER or android["libc"] != "bionic"
                or sha256(driver).hexdigest() != RETROID_DRIVER_SHA256):
            raise ValueError(f"Use the original Android driver from {RETROID_DRIVER}")
        apk.writestr("lib/arm64-v8a/libvulkan_freedreno.so", driver, compress_type=ZIP_STORED)
        for item in release.namelist():
            if item == "NOTICE" or item.startswith("LICENSES/"):
                apk.writestr("assets/licenses/DD-Turnip/" + item, release.read(item))
    for name in ("libadrenotools.so", "src/hook/libmain_hook.so", "src/hook/libhook_impl.so"):
        library = loader_build / name
        apk.write(library, "lib/arm64-v8a/" + library.name, compress_type=ZIP_STORED)
    apk.write(loader_source / "LICENSE", "assets/licenses/AdrenoTools.txt")
    apk.write(loader_source / "lib/linkernsbypass/LICENSE", "assets/licenses/linkernsbypass.txt")


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
        if args.retroid_turnip:
            manifest = manifest.replace('android:extractNativeLibs="false"', 'android:extractNativeLibs="true"')
            manifest = manifest.replace('android:versionName="0.1.26"', 'android:versionName="0.1.26-retroid-turnip"')
        manifest = manifest.replace(f'package="{PACKAGE}"', f'package="{args.application_id}"')
        for activity in ("VeilActivity", "SetupActivity", "CrashReportActivity"):
            manifest = manifest.replace(f'android:name=".{activity}"', f'android:name="{PACKAGE}.{activity}"')
        manifest = manifest.replace('android:name=".CrashReportProvider"',
                                    f'android:name="{PACKAGE}.CrashReportProvider"')
        manifest = manifest.replace(f'android:authorities="{PACKAGE}.crashreports"',
                                    f'android:authorities="{args.application_id}.crashreports"')
        engine_pin = None
        library = args.library
        if args.local_engine is not None:
            with args.local_engine.open("rb") as source:
                engine_pin = file_digest(source, "sha256").hexdigest() + "\n"
            manifest = manifest.replace('android:value="oaoriginal"', 'android:value="oabootstrap"')
            library = work / "liboabootstrap.so"
            clang = args.ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
            run(clang, "--target=aarch64-linux-android29", "-shared", "-O2", "-Wall", "-Wextra",
                "-Wl,-z,max-page-size=16384", "-o", library, ROOT / "native/android_bootstrap.c",
                "-landroid", "-ldl", "-llog")
        template = work / "AndroidManifest.xml"
        template.write_text(manifest, encoding="utf-8")
        resources = work / "resources.zip"
        run(tools / "aapt2.exe", "compile", "--dir", ROOT / "android/res", "-o", resources)
        base = work / "base.apk"
        run(tools / "aapt2.exe", "link", "-I", android, "--manifest", template, "-o", base, resources)

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
            for entry in compiled.infolist():
                apk.writestr(entry, compiled.read(entry))
            apk.write(work / "classes.dex", "classes.dex")
            library_name = "liboabootstrap.so" if engine_pin is not None else "liboaoriginal.so"
            apk.write(library, "lib/arm64-v8a/" + library_name, compress_type=ZIP_STORED)
            if args.retroid_turnip:
                add_retroid_driver(apk, args.retroid_turnip, args.adrenotools_build, args.adrenotools_source)
            if args.offline:
                apk.writestr("assets/local-build/native-reflection", "1\n")
            if engine_pin is not None:
                apk.writestr("assets/local-build/engine.sha256", engine_pin)
                if args.download_engine:
                    version, = re.findall(r'android:versionName="([0-9]+\.[0-9]+\.[0-9]+)"', manifest)
                    apk.writestr("assets/local-build/engine.url",
                                 f"{ENGINE_RELEASES}/v{version}/liboaoriginal.so\n")
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
            for sound in sorted((ROOT / "android/ambience").glob("*.pcm")):
                apk.write(sound, "assets/ambience/" + sound.name)
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
    for name in ("sdk", "jdk", "keystore", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    engine = parser.add_mutually_exclusive_group(required=True)
    engine.add_argument("--library", type=Path, help="embed an engine in the APK")
    engine.add_argument("--local-engine", type=Path, help="record the expected engine hash without embedding it")
    parser.add_argument("--ndk", type=Path, help="Android NDK used to build the launcher")
    parser.add_argument("--download-engine", action="store_true",
                        help="download the matching liboaoriginal.so from GitHub during setup")
    parser.add_argument("--offline", action="store_true",
                        help="package an engine built with native shader reflection for offline setup")
    parser.add_argument("--veil-edits", type=Path)
    parser.add_argument("--veil-properties", type=Path)
    parser.add_argument("--key-alias", required=True)
    parser.add_argument("--password-env", help="name of an environment variable with the keystore password")
    parser.add_argument("--application-id", default=PACKAGE)
    parser.add_argument("--waterfall", type=Path, help="package the prepared Veil portrait loop")
    parser.add_argument("--prototype-ui", type=Path)
    parser.add_argument("--retroid-turnip", type=Path, help="DD-Turnip v0.8.0 release ZIP")
    parser.add_argument("--adrenotools-build", type=Path)
    parser.add_argument("--adrenotools-source", type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z][\w]*(?:\.[A-Za-z][\w]*)+", args.application_id, re.ASCII):
        parser.error("invalid Android application ID")
    if (args.veil_edits is None) != (args.veil_properties is None):
        parser.error("provide both Veil files or omit both for original menus")
    if args.local_engine is not None and args.ndk is None:
        parser.error("--local-engine requires --ndk")
    if args.download_engine and args.local_engine is None:
        parser.error("--download-engine requires --local-engine")
    if args.retroid_turnip and not (args.library and args.adrenotools_build and args.adrenotools_source):
        parser.error("--retroid-turnip requires --library, --adrenotools-build and --adrenotools-source")
    if args.offline and args.library is None:
        parser.error("--offline requires --library")
    if args.waterfall is not None and (
        not args.waterfall.is_file() or args.waterfall.stat().st_size != WATERFALL_BYTES
    ):
        parser.error("--waterfall requires the complete 100-frame, 256 × 320 BC3 loop")
    build(args)


if __name__ == "__main__":
    main()
