# Install and build Open Amalur

## Install on Android

Install the signed APK from [GitHub Releases](https://github.com/Mokomis/open-amalur-android/releases/latest) and select your Re-Reckoning folder. Starting with v0.1.6, the APK includes the ARM64 engine and its shader reflection support, so setup can finish offline.

Install updates over the existing app to preserve saves, settings, and touch layouts. The release APK includes the Allestar Glade waterfall used in Veil portraits.

## Build from source

Use these steps if you want to develop or mod the engine. The build translates the supported Windows x86 binaries to C and compiles that C to Android ARM64. It runs on a Windows x86-64 PC.

### Inputs

- A supported copy of Kingdoms of Amalur: Re-Reckoning, version `cs:15032` (Steam build 10619381). Set the game directory to the folder with `koa.exe` and `binkw32.dll`.
- Python 3.12 x64 and the dependency in [requirements.txt](requirements.txt).
- [LLVM-mingw 20261006](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261006), Windows x86-64 UCRT. Use its `bin` directory.
- [Android NDK r30](https://developer.android.com/ndk/downloads) for Windows, version `30.0.16248370`. The Veil hooks verify generated ARM instructions and require this compiler.

### Compile the engine

From a checkout, run these PowerShell commands after setting the three directories to your local paths.

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt

$game = 'C:\Program Files (x86)\Steam\steamapps\common\Kingdoms of Amalur Re-Reckoning'
$llvm = 'C:\tools\llvm-mingw-20261006-ucrt-x86_64\bin'
$ndk = 'C:\tools\android-ndk-r30'

.\.venv\Scripts\python.exe tools/build_native.py `
    --game-dir "$game" `
    --llvm-bin "$llvm" --ndk "$ndk" --output build
```

The library is `build/liboaoriginal.so`. The build prints its SHA-256 and leaves per-module translation logs and compiler diagnostics in the output directory. Use a new output directory for another build. `--jobs` sets the compiler worker count.

## Modding the generated engine

Add `--prepare-only` to stop after translation and source transformations. The generated C in `build/guest` and `build/guest-helpers` can be inspected or edited. Keep guest addresses and calling conventions consistent when changing hooks. To compile your changes, run:

```powershell
.\.venv\Scripts\python.exe tools/build_native.py --compile-only --ndk "$ndk" --output build
```

Project-written translation and instruction lowering live in `translator/`. The Android ABI, process, input and file adapters live in `native/`. [The native build plan](native/build-plan.json) records the exact sources and flags. It includes the synchronization, thread placement, frame presentation, visibility and Vulkan renderer components from the tested Android build. The authored shaders are under `native/shaders`. Android launcher and Veil importer sources are under `android/`.

The project-authored code is [MIT licensed](LICENSE). [Third-party notices](THIRD_PARTY_NOTICES.md) apply to the included dependencies. Generated C and game inputs stay on the build machine. The engine reads DXBC shader metadata through its native implementation of `D3DReflect`.

## Assemble an APK

`tools/build_apk.py` compiles the Android app with SDK Platform 35, Android Build Tools 36.0.0 and JDK 21. It accepts the library produced above and your own APK signing key. The resulting APK can use the original game menus, and supplying both `--veil-edits` and `--veil-properties` applies your Veil edits.

```powershell
$java = 'C:\Program Files\Java\jdk-21'
$sdk = "$env:LOCALAPPDATA\Android\Sdk"
$env:JAVA_HOME = "$java"
& "$java\bin\keytool.exe" -genkeypair -keystore build\local.jks -alias openamalur -keyalg RSA -keysize 2048 -validity 10000
.\.venv\Scripts\python.exe tools/build_apk.py --sdk "$sdk" --jdk "$java" `
    --library build/liboaoriginal.so --offline --keystore build/local.jks --key-alias openamalur `
    --application-id org.openamalur.local --output build/OpenAmalur-local.apk
```

Use a distinct `--application-id` when installing a build signed with your own key alongside an existing build.

To assemble the offline release APK, keep `--library` and `--offline`, supply the Veil recipe and pause overlay, and use the release application ID and signing key. The APK includes the ARM64 engine. Setup imports the selected game files directly from Android storage.

Extract the build-assets ZIP from the matching [GitHub release](https://github.com/Mokomis/open-amalur-android/releases) and pass `veil-edits.bin` to `--veil-edits`, `veil-edits.properties` to `--veil-properties`, `oa_prototype_ui.bin` to `--prototype-ui`, and `veil-waterfall.bc3` to `--waterfall`. The included `engine.sha256` gives the expected hash of the locally built library.

The builder adds the prepared Allestar Glade loop as `assets/veil-waterfall.bc3`. The portrait renderer plays the five-second loop at 20 frames per second. `tools/make_waterfall_clip.py` can re-encode an existing capture during release preparation.

`tools/generate_veil_edits.py` takes your original game directory, a locally modded `patch_0.pak`, and output paths for the recipe, properties and a local reference archive. Use the original and modded archives from the same supported game version.

`tools/retarget_veil_fonts.py` renders [Jost](https://github.com/google/fonts/tree/main/ofl/jost) into the existing Veil glyph bounds while retaining the layout metrics. Install Pillow, fonttools, NumPy and SciPy for this step. Extract the `savapro_medium` and `byngve_bold` `.font` files and matching `_p00_00.ntx` atlases from the local Veil and retail archives. Name the extracted files with `current-` and `retail-` prefixes, respectively. Pass the Jost font file, the directory of extracted files and a new output directory to the script. Supply `--prototype` with the pause overlay file to rebuild its atlas too.

Pass the resulting menu atlas files to `generate_veil_edits.py --replace` when regenerating the Veil recipe. Pass the prior recipe's properties with `--previous-metadata` so installed copies can upgrade. Supply the regenerated pause overlay through `build_apk.py --prototype-ui`. The APK builder includes [the Jost license](android/licenses/Jost-OFL.txt).

`tools/fit_veil_panels.py` and `tools/mark_veil_portraits.py` take a compatible local Veil archive and an output folder. They produce the responsive quest, save, equipment and character creation layouts, the portrait rendering hooks, and the opening cutscene markers for `--replace`.

`tools/mark_veil_name_dialog.py` takes the Veil archive and writes a replacement UI script batch. Use it after applying the portrait and cutscene changes. The markers let the original dialog receive touches while it owns modal input.

`tools/fit_veil_inventory.py` takes the Veil archive with the name-dialog changes and writes two inventory layouts and an updated UI script batch. Run `tools/patch_veil_environment.py` on that generated batch before passing the three replacements to `generate_veil_edits.py`. The APK builder includes the ambience clips and their credits automatically.

The launcher reads shared game files directly. It checks the game version and validates the copied files, creates a local profile, and detects Fatesworn from its optional archive in `data_1278260`. The title screen's Game Files action can import the same archive into an existing install. The GitHub update badge is independent of game startup once the required files are installed.

## Verification

A fresh Windows checkout regenerated the game, Bink and DirectX compiler translations from local files, compiled the 362 native objects, and linked the Android library. The stock-menu development APK built with that library imported a copied game folder on an OPPO Pad Mini (OPD2515) with Android Internet permission omitted. The engine mounted Fatesworn `cs:13757` when the matching archive was present and reached the base-game menu after the archive was removed from the test installation.

Two independent Windows builds of the Veil 0.1.3 engine produced identical libraries. On the OPPO Pad Mini, the release launcher imported the full game offline, rejected a mismatched engine file, and launched Veil with the locally built engine and Fatesworn. The generated waterfall played in the Patron portrait. An in-place offline upgrade over the prior Veil build reused its game files and preserved the saves, graphics settings and touch layout.
