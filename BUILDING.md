# Build the Android native library

The build translates the supported Windows x86 binaries to C and compiles that C to Android ARM64. Use a Windows x86-64 PC for the build. An Android device can run the resulting library inside an APK.

## Inputs

- A supported Steam installation of Kingdoms of Amalur: Re-Reckoning, version `cs:15032` (Steam build 10619381). Set the game directory to the folder with `koa.exe` and `binkw32.dll`.
- The matching 32-bit Microsoft `d3dcompiler_43.dll`, typically in `C:\Windows\SysWOW64` after installing the legacy DirectX runtime. The builder checks all three files and rebases a copy of the compiler DLL in its output directory.
- Python 3.12 x64 and the dependency in [requirements.txt](requirements.txt).
- [LLVM-mingw 20261006](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261006), Windows x86-64 UCRT. Use its `bin` directory.
- [Android NDK r30](https://developer.android.com/ndk/downloads) for Windows, version `30.0.16248370`. The Veil hooks verify generated ARM instructions and require this compiler.

## Build

From a checkout, run these PowerShell commands after setting the three directories to your local paths.

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt

$game = 'C:\Program Files (x86)\Steam\steamapps\common\Kingdoms of Amalur Re-Reckoning'
$llvm = 'C:\tools\llvm-mingw-20261006-ucrt-x86_64\bin'
$ndk = 'C:\tools\android-ndk-r30'

.\.venv\Scripts\python.exe tools/build_native.py `
    --game-dir "$game" `
    --compiler-dll "$env:WINDIR\SysWOW64\d3dcompiler_43.dll" `
    --llvm-bin "$llvm" --ndk "$ndk" --output build
```

The library is `build/liboaoriginal.so`. The build prints its SHA-256 and leaves per-module translation logs and compiler diagnostics in the output directory. Use a new output directory for another build. `--jobs` sets the compiler worker count.

## Install with the release APK

Build the engine from the matching release tag on Windows, then copy `build/liboaoriginal.so` to shared storage on Android. Install the signed release APK and choose your Re-Reckoning folder. When prompted, choose the engine file you built. The app verifies the library against the SHA-256 in the signed APK before loading it. Building and installing work offline once the listed tools and game files are available.

An update reuses the installed engine when its SHA-256 is unchanged. If a release changes the engine, rebuild from that release tag and select the new library at the prompt. Keep the app installed when upgrading to preserve its saves, settings and touch layout.

### Generate the portrait waterfall

Record a fixed view of the waterfall in Allestar Glade just outside the tutorial. Hold the camera still for at least eight seconds with the waterfall in the central portrait area and the character and controls outside the crop. The recording can come from the PC game or Open Amalur on Android.

With FFmpeg on PATH, install Pillow in the build environment and create the five-second loop.

```powershell
.\.venv\Scripts\python.exe -m pip install Pillow==13.0.0
.\.venv\Scripts\python.exe tools/make_waterfall_clip.py glade.mp4 veil-waterfall.bc3
```

Adjust the portrait framing with `--crop width:height:left:top` in pixels and the start time with `--start` in seconds. Put `veil-waterfall.bc3` in the game folder before first import, or add it later with **Game Files → Import Waterfall**, then reopen the game. The loop plays at 20 frames per second in the same Veil portrait panels. Game setup and play are available before a clip is added.

## Modding the generated engine

Add `--prepare-only` to stop after translation and source transformations. The generated C in `build/guest` and `build/guest-helpers` can be inspected or edited. Keep guest addresses and calling conventions consistent when changing hooks. To compile your changes, run:

```powershell
.\.venv\Scripts\python.exe tools/build_native.py --compile-only --ndk "$ndk" --output build
```

Project-written translation and instruction lowering live in `translator/`. The Android ABI, process, input and file adapters live in `native/`. [The native build plan](native/build-plan.json) records the exact sources and flags. It includes the synchronization, thread placement, frame presentation, visibility and Vulkan renderer components from the tested Android build. The authored shaders are under `native/shaders`. Android launcher and Veil importer sources are under `android/`.

The project-authored code is [MIT licensed](LICENSE). [Third-party notices](THIRD_PARTY_NOTICES.md) apply to the included dependencies. Translation outputs, captures and local game inputs stay on the player's machines.

## Assemble an APK

`tools/build_apk.py` compiles the Android app with SDK Platform 35, Android Build Tools 36.0.0 and JDK 21. It accepts the library produced above and your own APK signing key. The resulting APK can use the original game menus, and supplying both `--veil-edits` and `--veil-properties` applies your Veil edits.

```powershell
$java = 'C:\Program Files\Java\jdk-21'
$sdk = "$env:LOCALAPPDATA\Android\Sdk"
$env:JAVA_HOME = "$java"
& "$java\bin\keytool.exe" -genkeypair -keystore build\local.jks -alias openamalur -keyalg RSA -keysize 2048 -validity 10000
.\.venv\Scripts\python.exe tools/build_apk.py --sdk "$sdk" --jdk "$java" `
    --library build/liboaoriginal.so --keystore build/local.jks --key-alias openamalur `
    --application-id org.openamalur.local --output build/OpenAmalur-local.apk
```

Use a distinct `--application-id` when installing a build signed with your own key alongside an existing build.

To assemble a release launcher, replace `--library build/liboaoriginal.so` with `--local-engine build/liboaoriginal.so --ndk "$ndk"` and supply the Veil recipe and pause overlay. The APK records the engine SHA-256 and packages the launcher. Pass the release application ID and use its existing signing key for a normal Android upgrade.

`tools/generate_veil_edits.py` takes your original game directory, a locally modded `patch_0.pak`, and output paths for the recipe, properties and a local reference archive. Use the original and modded archives from the same supported game version.

`tools/retarget_veil_fonts.py` renders [Jost](https://github.com/google/fonts/tree/main/ofl/jost) into the existing Veil glyph bounds while retaining the layout metrics. Install Pillow, fonttools, NumPy and SciPy for this step. Extract the `savapro_medium` and `byngve_bold` `.font` files and matching `_p00_00.ntx` atlases from the local Veil and retail archives. Name the extracted files with `current-` and `retail-` prefixes, respectively. Pass the Jost font file, the directory of extracted files and a new output directory to the script. Supply `--prototype` with the pause overlay file to rebuild its atlas too.

Pass the resulting menu atlas files to `generate_veil_edits.py --replace` when regenerating the Veil recipe. Pass the prior recipe's properties with `--previous-metadata` so installed copies can upgrade. Supply the regenerated pause overlay through `build_apk.py --prototype-ui`. The APK builder includes [the Jost license](android/licenses/Jost-OFL.txt).

`tools/fit_veil_panels.py` and `tools/mark_veil_portraits.py` take a compatible local Veil archive and an output folder. They produce the responsive quest, save and equipment layouts, the portrait visibility hooks, and the opening cutscene markers for `--replace`.

The launcher works from shared game files without Steam or Internet access. It checks the game version and validates the copied files, creates a local profile, and detects Fatesworn from its optional archive in `data_1278260`. The title screen's Game Files action can import the same archive into an existing install. The GitHub update badge is independent of game startup.

## Verification

A fresh Windows checkout regenerated the game, Bink and DirectX compiler translations from local files, compiled the 362 native objects, and linked the Android library. The stock-menu development APK built with that library imported a copied game folder on an OPPO Pad Mini (OPD2515) with Android Internet permission omitted. The engine mounted Fatesworn `cs:13757` when the matching archive was present and reached the base-game menu after the archive was removed from the test installation.

Two independent Windows builds of the Veil 0.1.3 engine produced identical libraries. On the OPPO Pad Mini, the release launcher imported the full game offline, rejected a mismatched engine file, and launched Veil with the locally built engine and Fatesworn. The generated waterfall played in the Patron portrait. An in-place offline upgrade over the prior Veil build reused its game files and preserved the saves, graphics settings and touch layout.
