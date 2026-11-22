"""Prepare, build and check an Open Amalur for Android release.

    python tools/release.py bump 0.1.28
        Set versionName/versionCode in the manifest and the README download line.

    python tools/release.py build --config release.json
        Build the signed APK with tools/build_apk.py (Windows), then verify it.

    python tools/release.py verify --apk open-amalur-android-0.1.28.apk [--sdk <android-sdk>]
        Check the APK's version against the manifest, its signer and alignment.

release.json names the local inputs, none of which belong in the repository:

    {
      "sdk": "C:/.../android-sdk",
      "jdk": "C:/Program Files/Eclipse Adoptium/jdk-21...",
      "keystore": "C:/.../signing/original-engine.keystore",
      "key_alias": "original",
      "password_file": "C:/.../signing/key.pass",
      "engine": "C:/.../liboaoriginal.so",
      "veil_edits": "C:/.../veil-edits.bin",
      "veil_properties": "C:/.../veil-edits.properties",
      "waterfall": "C:/.../veil-waterfall.bc3",
      "output_dir": "C:/.../oa-android-release",
      "signer_sha256": "26bb9dbf..."
    }

The keystore password is read from password_file into the build's environment
and is never printed or passed on the command line.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
MANIFEST = REPO / "android/AndroidManifest.xml"
README = REPO / "README.md"
VERSION = re.compile(r"[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}")
PASSWORD_ENV = "OA_SIGNING_PASSWORD"


def manifest_version():
    text = MANIFEST.read_text(encoding="utf-8")
    name = re.search(r'android:versionName="([^"]+)"', text)[1]
    code = int(re.search(r'android:versionCode="([0-9]+)"', text)[1])
    return name, code


def bump(version):
    if not VERSION.fullmatch(version):
        raise SystemExit(f"Version must look like 0.1.28, not {version!r}")
    old_name, old_code = manifest_version()
    if tuple(map(int, version.split("."))) <= tuple(map(int, old_name.split("."))):
        raise SystemExit(f"{version} is not newer than the current {old_name}")
    text = MANIFEST.read_text(encoding="utf-8")
    text = text.replace(f'android:versionCode="{old_code}"', f'android:versionCode="{old_code + 1}"', 1)
    text = text.replace(f'android:versionName="{old_name}"', f'android:versionName="{version}"', 1)
    MANIFEST.write_text(text, encoding="utf-8")
    readme = README.read_text(encoding="utf-8")
    line = re.compile(r"\*\*Open Amalur for Android v[0-9.]+\*\*(.*?)\[v[0-9.]+ release notes\]"
                      r"\(https://github\.com/Mokomis/open-amalur-android/releases/tag/v[0-9.]+\)")
    readme, count = line.subn(
        lambda m: f"**Open Amalur for Android v{version}**{m[1]}[v{version} release notes]"
                  f"(https://github.com/Mokomis/open-amalur-android/releases/tag/v{version})", readme, count=1)
    if count != 1:
        raise SystemExit("Could not find the README download line to update")
    README.write_text(readme, encoding="utf-8")
    print(f"{old_name} ({old_code}) -> {version} ({old_code + 1}). Review the diff, then commit.")


def build_tools(sdk):
    tools = sorted((sdk / "build-tools").iterdir(), key=lambda p: [int(x) for x in re.findall(r"[0-9]+", p.name)])
    if not tools:
        raise SystemExit(f"No build-tools in {sdk}")
    return tools[-1]


def tool(folder, name):
    for suffix in ("", ".exe", ".bat"):
        candidate = folder / (name + suffix)
        if candidate.is_file():
            return str(candidate)
    found = shutil.which(name)
    if found:
        return found
    raise SystemExit(f"{name} not found in {folder}")


def verify(apk, sdk=None, signer=None):
    name, code = manifest_version()
    tools = build_tools(sdk) if sdk else None
    run = lambda *argv: subprocess.run(argv, check=True, capture_output=True, text=True).stdout
    aapt2 = tool(tools, "aapt2") if tools else shutil.which("aapt2")
    apksigner = tool(tools, "apksigner") if tools else shutil.which("apksigner")
    zipalign = tool(tools, "zipalign") if tools else shutil.which("zipalign")
    problems = []
    badging = run(aapt2, "dump", "badging", str(apk))
    found = re.search(r"versionCode='([0-9]+)' versionName='([^']+)'", badging)
    if not found or (int(found[1]), found[2]) != (code, name):
        problems.append(f"APK version {found and found.groups()} does not match manifest ({code}, {name})")
    certs = run(apksigner, "verify", "--print-certs", str(apk))
    digest = re.search(r"certificate SHA-256 digest: ([0-9a-f]{64})", certs)
    if not digest:
        problems.append("APK signature did not verify")
    elif signer and digest[1] != signer.lower():
        problems.append(f"APK is signed by {digest[1]}, expected {signer}")
    if zipalign:
        if subprocess.run([zipalign, "-c", "-P", "16", "4", str(apk)], capture_output=True).returncode:
            problems.append("APK is not 16 KB page aligned")
    sha = hashlib.sha256(apk.read_bytes()).hexdigest()
    print(f"{apk.name}\n  version {name} ({code})\n  signer  {digest[1] if digest else 'none'}\n  sha256  {sha}")
    if problems:
        raise SystemExit("\n".join("FAIL " + p for p in problems))
    print("  OK")


def build(config_path):
    config = json.loads(Path(config_path).read_text(encoding="utf-8"))
    name, _ = manifest_version()
    output_dir = Path(config["output_dir"])
    output_dir.mkdir(parents=True, exist_ok=True)
    apk = output_dir / f"open-amalur-android-{name}.apk"
    if apk.exists():
        raise SystemExit(f"{apk} already exists")
    env = dict(os.environ)
    env[PASSWORD_ENV] = Path(config["password_file"]).read_text(encoding="utf-8").strip()
    command = [sys.executable, str(REPO / "tools/build_apk.py"),
               "--sdk", config["sdk"], "--jdk", config["jdk"], "--keystore", config["keystore"],
               "--output", str(apk), "--library", config["engine"], "--offline",
               "--key-alias", config["key_alias"], "--password-env", PASSWORD_ENV]
    for key, flag in (("veil_edits", "--veil-edits"), ("veil_properties", "--veil-properties"),
                      ("waterfall", "--waterfall")):
        if config.get(key):
            command += [flag, config[key]]
    subprocess.run(command, cwd=REPO, env=env, check=True)
    verify(apk, Path(config["sdk"]), config.get("signer_sha256"))


def main():
    cli = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = cli.add_subparsers(dest="command", required=True)
    commands.add_parser("bump").add_argument("version")
    commands.add_parser("build").add_argument("--config", required=True)
    check = commands.add_parser("verify")
    check.add_argument("--apk", required=True, type=Path)
    check.add_argument("--sdk", type=Path)
    check.add_argument("--signer", help="expected certificate SHA-256")
    args = cli.parse_args()
    if args.command == "bump":
        bump(args.version)
    elif args.command == "build":
        build(args.config)
    else:
        verify(args.apk, args.sdk, args.signer)


if __name__ == "__main__":
    main()
