# Working on Open Amalur

Rules for anyone changing this repository, including AI coding agents. `CLAUDE.md` points here.

Open Amalur statically recompiles the original x86 *Re-Reckoning* executable to ARM64 (`translator/`) and runs it on a hand-written runtime, Vulkan renderer and Android launcher (`native/`, `android/`). It is not a matching decompilation: translated code is generated, and the rules below are about the code people write.

## Game data structures

- **Never use a raw offset into a game structure in hand-written code.** Add the field to `native/android/app/guest_layout.h` and use its name: `ld32(item + ITEM_OWNER)`, not `ld32(item + 0x8c)`. A change that adds a raw offset is not done.
- **Define each structure and field once,** in `guest_layout.h`. Don't copy offsets into other files.
- **Name fields from evidence.** Use what the engine provably does with a field. If that isn't established, name it `UNKNOWN_<offset>` and write down what is known. Don't guess a meaning.
- Guest addresses are 32-bit values in the emulated address space, so layouts are named offsets read with `ld8`/`ld32`/`st32`, not C structs.
- Return addresses and global pointers in `koa.exe` get names too, next to the code that uses them or in `guest_layout.h`.
- Older hooks still use raw offsets. Convert a file to `guest_layout.h` when you change it; `veil_transmog.c` shows the pattern.

## Readability from the start

- Keep hand-written native code free of warnings. CI compiles it with `-Wall -Wextra -Werror` (`tools/check_native.py`).
- Prefer names that say what something is over comments that explain a number.
- Don't edit translated output. Changes to lifted game functions go through `native/guest-edits.json` or `native/derived-edits.json`.

## Proving refactors

A refactor that shouldn't change behaviour (renames, moving code, named offsets) should compile to **byte-identical objects**. Check it by compiling the affected objects with the `build-plan.json` flags on the Windows build machine, before and after, and comparing them. Relinking the v0.1.27 objects reproduces the shipping engine exactly, so that comparison is trustworthy.

## Checks

- `python -m unittest discover -s tools/tests` covers the host tests.
- `python tools/check_native.py --ndk <android-ndk>` compiles all hand-written native sources.
- `python tools/device_smoke_test.py --apk <apk>` installs a build as an update and checks the essentials on a connected device.
- CI (`.github/workflows/checks.yml`) runs the Android, Python and native checks on every pull request.

## Safety

- Never commit game files, extracted assets, signing keys or passwords. The keystore password is read from a file into an environment variable by the build scripts; never print it.
- On test devices, install builds over the existing app (`adb install -r`). Never uninstall: that deletes saves and the imported game.
