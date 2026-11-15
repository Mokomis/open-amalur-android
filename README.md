<img src="screenshots/open-amalur-icon.png" width="112" height="112" alt="Open Amalur icon">

Right. Let’s get this out of the way so we don’t waste each other’s time.

This started as a personal experiment. I wanted to see if I could use AI to get *Kingdoms of Amalur: Re-Reckoning* running natively on Android.

Open Amalur is unequivocally an AI-first project. I led the work and used ChatGPT, Claude, and Qwen throughout. If that’s a deal-breaker for you, this is your cue to walk away.

Open Amalur is a recompilation of the original *Re-Reckoning* engine. The translator and runtime are open source. Starting with v0.1.6, the APK includes the prebuilt ARM64 engine and sets up the game offline using files from your copy.

I substantially reworked the engine’s threading to improve how it schedules simulation, rendering, and background jobs across multiple CPU cores.

Android is the first release. I plan to build a Windows version next. Some of the work on threading, graphics, and the interface could also carry over to macOS, Linux, and iOS if someone wants to take it there.

## About the UI

The UI got a huge overhaul. I loathed the original interface with a passion, and this was my chance to change it. So I did.

Veil draws visual inspiration from other interfaces, including Edge for Skyrim. I developed the UI changes and the other enhancements for Open Amalur without downloading, sampling, or reusing files or code from those mods.

## Download and install

1. Download the APK directly to your Android device and install it. [GitHub Releases](https://github.com/Mokomis/open-amalur-android/releases/latest)
2. Open Open Amalur and choose the Re-Reckoning folder containing `koa.exe` and `data`.
3. Setup imports your game files, applies Veil, and starts the game with the engine bundled in the APK.

With the v0.1.6 APK and game files on your device, setup and play work offline. Install updates over your existing copy of Open Amalur to keep your saves, settings, and touch layout.

### Game files

Setup validates the files in the selected folder directly and creates a local profile. The tested Re-Reckoning version is `cs:15032` (Steam build ID `10619381`). Keep the original folder structure, including the English, French, and German game archives and the animated title screen video at `data/videos/well.bik`.

Choose a folder that Android’s file picker can access. If your game files are in DroidDeck’s private storage, copy the full game folder to shared storage, such as `Documents/Amalur`. [Android file picker access](https://developer.android.com/training/data-storage/shared/documents-files) · [DroidDeck](https://github.com/Droid-Deck/DroidDeck)

### Fatesworn

Setup detects and imports Fatesworn if the game folder contains the supported `data_1278260/dlc_mit_0.pak` archive. To add it later, choose **Game Files → Add Fatesworn** from the title screen and select the updated game folder or its `data_1278260` folder. Restart the app to load the expansion.

## How the engine runs

We built the **native ARM64 engine** by translating the original executable’s x86 machine code into C and compiling it for Android. It uses the game’s original data formats and scripts.

- **Engine behavior.** We used matched inputs to compare the Android build with the original PC game, checking instruction handling, floating-point behavior, animation blending, skinning, and GPU submission.
- **Android integration.** The runtime provides file access, input, audio, local profiles, saves, and display handling. The graphics layer translates the engine’s Direct3D 11 calls to Vulkan. The native `D3DReflect` implementation reads metadata from the game’s compiled shaders.
- **Multicore threading.** We reworked how the engine discovers available CPU cores, synchronizes jobs, and wakes workers as jobs finish. We profiled simulation, graphics recording, and background work across multiple cores on the OPPO Pad Mini.

In a clean OPPO Pad Mini test with networking disabled, the signed v0.1.6 APK imported the game files, detected Fatesworn, and reached the Veil title screen. A tap on **OK** confirmed the character name and advanced the opening scene. The full campaign and expansion still need broader playtesting.

[Engine and threading notes](ENGINE.md) · [Developer build guide](BUILDING.md#build-from-source)

## Added features

### Veil interface

Veil uses compact lists and a consistent set of buttons, tabs, and prompts across the menus. The APK includes the animated Allestar Glade waterfall used in Veil portraits. The menu font is Jost. [Edge UI](https://www.nexusmods.com/skyrimspecialedition/mods/130983) · [Jost license](android/licenses/Jost-OFL.txt)

- **Main and pause menus** with a redesigned options sidebar and dedicated settings lists.
- **Inventory, stash, and equipment** with compact item lists, a separate details pane, equipped-item markers, and appearance controls. Consumables can be mapped to the Item Radial. Weapons use Primary and Secondary slots.
- **Character and progression screens** for stats, skills, abilities, destinies, and character creation. The four character creation screens use Veil-framed portraits with the animated Allestar Glade waterfall.
- **Quest log and maps** with side-by-side quest lists and objectives, pinch-to-zoom on maps, and touch confirmation for quest tutorials.
- **Save and load** with full-screen slots and a touch action for creating or overwriting a save. Overwriting keeps the game's confirmation prompt.
- **Dialogue** with tap selection, a draggable wheel in conversations that use one, and an on-screen Skip control.
- **Trading, stash, and NPC services** including buying, selling, repair, training, and healing. Veil also covers alchemy, blacksmithing, sagecrafting, lockpicking, and the timed Dispel screen.

### Gameplay and controls

- **Transmog** changes the look of equipped armor and same-type weapons, such as a sword taking the appearance of another sword. The equipped item retains its stats. Appearance choices are saved with the character.
- **Tablet controls** provide separate movement and camera sticks, Run/Leap, attacks, dodge, block, interaction, and shortcuts for the game menu, inventory, world map, quests, and Item Radial.
- **Touch layout editor** moves and resizes individual controls and saves the layout.
- **Ability and Reckoning controls** include a tappable 1–9/0 ability bar, a Cast button for the selected spell, on-screen LT and RT prompts, touch finishers, and Continue buttons on screens that require a controller button or key.
- **Controller support** covers gameplay, menu navigation and vibration on controllers that expose a vibrator to Android.
- **Camera Bounding** limits how far the camera pulls back during group fights.
- **Outdoor ambience** plays occasional wildlife sounds according to the in-game clock. Day Ambience and Night Ambience have separate switches in Audio and follow Effects Volume. Wildlife gets quieter during loud game effects.

### Graphics and Android integration

- **Android Hardware Scaling** offers Off, 25%, 50%, 75%, and 100%. The setting defaults to 100% and takes effect after a restart. Choose **Native** resolution to match the display’s aspect ratio for gameplay and in-engine cinematics.
- **Shadow Map Resolution** offers 512, 1024, 1536, and 2048 px.
- **Shadow Distance** and **Vegetation Draw Distance** each offer 75, 100, 125, and 150 percent.
- **Ambient Occlusion** offers Off, On, and High. **Depth of Field** offers 0, 50, 100, and 150 percent. The menu identifies settings that take effect after a restart.
- **Darker Nights** deepens shadows outdoors after dusk while preserving bright highlights. The setting defaults to Off and takes effect immediately.
- **Direct startup** opens the title menu when setup is complete.
- **First-launch import** copies game files from the selected Android folder and applies Veil automatically.
- **GitHub release status** appears on the main menu and links to the release page when a newer version is available.

### Screenshots

Captured in v0.1.5 on the OPPO Pad Mini. Select an image to open it at full size.

| Touch controls | Inventory and equipment details |
| --- | --- |
| [![Gameplay with movable touch controls](screenshots/touch-controls.jpg)](screenshots/touch-controls.jpg) | [![Equipped armor, item stats and the animated portrait background](screenshots/inventory.jpg)](screenshots/inventory.jpg) |
| **Character screen** | **Quest log** |
| [![Character screen with the waterfall backdrop](screenshots/character.jpg)](screenshots/character.jpg) | [![Quest list alongside objectives](screenshots/quest-log.jpg)](screenshots/quest-log.jpg) |
| **Dialogue options** | **Save screen** |
| [![Dialogue options in a conversation with Aewin Rathgar](screenshots/dialogue.jpg)](screenshots/dialogue.jpg) | [![Save slots with the selected save’s preview and details](screenshots/save.jpg)](screenshots/save.jpg) |
| **Title screen and update status** | **First-launch folder setup** |
| [![Main menu showing GitHub release status](screenshots/title-updates.jpg)](screenshots/title-updates.jpg) | [![First-launch prompt to choose the Re-Reckoning folder](screenshots/setup.jpg)](screenshots/setup.jpg) |
| **Veil Graphics** | |
| [![Graphics options with shadow resolution and distance, vegetation draw distance, ambient occlusion, and depth of field](screenshots/graphics.jpg)](screenshots/graphics.jpg) | |

Open Amalur is a fan project and is independent of THQ Nordic and Kaiko.

## AI contributors

- ChatGPT
- Claude
- Qwen
