# Ocarina of Time VR for Steam Frame

This is my Steam Frame fork of [Ship of Harkinian VR](https://github.com/ChocoTorp/Shipwright-VR). It runs on the headset through Lepton, Android, SteamVR OpenXR and GLES; no gaming PC is needed.

**EXPERIMENTAL.** This is a personal-use port and remains a work in progress. It targets the Valve Steam Frame. You need your own compatible Ocarina of Time game data; it is not included. The separate Frame build is assembled from public source and this fork's tracked changes.

The original Ship of Harkinian VR port supplies the core VR game and interactions. This fork adds the Frame build, controller support, persistent storage, camera controls and presentation changes.

## VR features and Frame changes

- Head-tracked stereoscopic play, with snap or smooth turning and seated recentering.
- Frame controller bindings: the right controller's physical Menu button pauses the game, and left View recentres. The SteamVR system/dashboard button is not bound.
- A laser pointer for menus, plus an in-headset VR settings menu.
- A 3D cinema option for cutscenes and pre-rendered rooms, with immersive presentation available in settings.
- A pause view that can show the frozen game world behind the menu, and a head-level HUD. The paused VR settings menu previews camera changes live; in third-person play, hold left grip and move the right stick to adjust world size, camera distance and height.
- Persistent game files, saves and settings in `~/Documents/OoTVR`.

In third-person play, left trigger targets and the right bumper raises the shield. The third-person camera also supports right-stick turning and C-up/down.

The game runs at its native logic rate while the headset presents interpolated frames. Busy scenes can show motion between the game's updates, and the Frame build exposes a stereo render divisor and display settings for tuning.

The [Steam Frame guide](docs/STEAM_FRAME.md) covers building, installation, controls and settings. The [architecture notes](docs/VR_ARCHITECTURE.md) explain how the port works. The Quest README remains available in [README_QUEST.md](README_QUEST.md).

## Build and install

The Frame APK is built from the `frame` Gradle variant. Start with the prerequisites and commands in the [Steam Frame guide](docs/STEAM_FRAME.md); its build produces the APK, and its deploy script installs the title and copies your own game data to the headset.

## Credits and licenses

This fork is based on [Ship of Harkinian VR](https://github.com/ChocoTorp/Shipwright-VR) and its upstream engine. This repository has no root license; check the upstream project and component notices before reusing code. The Quest README is preserved for the original port's setup and gameplay details. Game data, extracted assets and saves come from your own copy.
