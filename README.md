# Open Amalur for Android

Open Amalur is an unofficial Android project for **Kingdoms of Amalur: Re-Reckoning**. Its aim is to preserve the PC game's combat, quests, characters, and world while making the game practical to play on a tablet with touch controls or a controller.

This repository is the project page for the current original-engine work. The earlier [Open Amalur research archive](https://github.com/Mokomis/open-amalur) remains available. The first downloadable build is being prepared. Supported game versions and installation instructions will accompany the release.

## How the engine runs

The first approach reconstructed parts of the engine from the game's data. It made selected locations playable, but the work exposed how much behavior and rendering would need to be recreated. The current project starts from the original Windows x86 engine and uses its behavior on Windows as a reference.

1. Recover and test bounded parts of the original engine. The early comparisons followed animation jobs through expression evaluation, blend trees, transforms, skinning, and GPU submission. The tests checked Windows results against the original x86 program and then ran the corresponding compiled code on ARM.
2. Repair the x86-to-C translation path. We qualified instruction handling, register and flag behavior, floating-point operations, and Windows calling conventions against the original program. The current Android build compiles translated code to ARM64 ahead of time.
3. Provide the Windows services the game expects. Android adapters handle threads and synchronization, file access, input, audio, local profiles and saves, and window and surface events. The graphics layer maps the game's Direct3D 11 work to Vulkan. On-device tests progressed from isolated scenes and captured graphics to live gameplay.
4. Rebuild the original engine’s threading for Android. A substantial part of the port went into getting the original engine to distribute its work across multiple ARM CPU cores. We rebuilt the Windows threading and synchronization services the engine relies on, corrected how it discovers available cores, and changed how waiting workers are notified when jobs complete. We profiled the simulation, graphics recording, and background workers on the OPPO against matched gameplay scenes.

The live tests so far are concentrated on the OPPO Pad Mini. Performance varies with scene, resolution, device, and temperature. Compatibility across the full campaign and the Fatesworn expansion is still being checked.

[Read the engine and threading notes](ENGINE.md).

## Veil

Veil takes its visual cues from [Edge UI for Skyrim](https://www.nexusmods.com/skyrimspecialedition/mods/130983). It brings compact lists and item details to inventory, quests, saves, and other menus while keeping the original game's actions behind them.

- Touch controls with movable, resizable buttons, menu shortcuts, two sticks, and a tappable numbered ability bar.
- Tap and drag selection on the dialogue wheel, touch confirmations for key prompts, and double-tap saving with the original overwrite confirmation.
- An updated character screen with an animated waterfall backdrop and appearance changes for armor and same-type weapons.
- Graphics options for shadow quality and distance, draw distance, ambient occlusion, and depth of field. Camera Bounding reduces the extra camera pullback during group combat.

### Screenshots

Captured on the OPPO Pad Mini. Select an image to open it at full size.

| Touch controls | Inventory and weapon appearance |
| --- | --- |
| [![Gameplay with movable touch controls](screenshots/touch-controls.jpg)](screenshots/touch-controls.jpg) | [![Weapon list, stats and equipped appearance](screenshots/inventory.jpg)](screenshots/inventory.jpg) |
| **Character screen** | **Quest log** |
| [![Character screen with the waterfall backdrop](screenshots/character.jpg)](screenshots/character.jpg) | [![Quest list alongside objectives](screenshots/quest-log.jpg)](screenshots/quest-log.jpg) |
| **Dialogue wheel** | **Save confirmation** |
| [![Dialogue wheel in a conversation with Agarth](screenshots/dialogue.jpg)](screenshots/dialogue.jpg) | [![Confirmation before overwriting a save](screenshots/save.jpg)](screenshots/save.jpg) |
| **Title screen and update status** | **First-launch folder setup** |
| [![Main menu showing GitHub release status](screenshots/title-updates.jpg)](screenshots/title-updates.jpg) | [![First-launch prompt to choose the Re-Reckoning folder](screenshots/setup.jpg)](screenshots/setup.jpg) |

## Game files

You will need your own supported PC installation of Re-Reckoning. This repository contains project documentation. Commercial game files, extracted game assets, generated engine translations, saves, and signing keys stay private.

The tested candidate recognizes Steam build 10619381 of Re-Reckoning. First-launch setup asks for the game folder and the matching 32-bit Microsoft DirectX shader compiler file. Keep the English, French, and German game archives together because the engine opens all three at startup.

The Android folder picker can see game folders on shared storage. If your DroidDeck library is installed inside DroidDeck's private storage, copy the game folder into a shared Steam library or a folder such as `Documents/Amalur` before importing. [Android file picker access](https://developer.android.com/training/data-storage/shared/documents-files) · [DroidDeck](https://github.com/Droid-Deck/DroidDeck)

Open Amalur is a fan project and is independent of THQ Nordic and Kaiko.
