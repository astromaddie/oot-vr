# Ocarina of Time VR for Steam Frame

This is my Steam Frame fork of [Ship of Harkinian VR](https://github.com/ChocoTorp/Shipwright-VR). It runs on the headset through Lepton, Android, SteamVR OpenXR and GLES; no gaming PC is needed.

**EXPERIMENTAL.** This is a personal-use port and remains a work in progress. It targets the Valve Steam Frame. You need your own compatible Ocarina of Time game data; it is not included. The separate Frame build is assembled from public source and this fork's tracked changes.

**Want to play on Steam Frame?** Download the prebuilt standalone APK from the [v0.1.0 prerelease](https://github.com/astromaddie/oot-vr/releases/tag/v0.1.0); no compiler is needed. Bring a compatible Ocarina of Time ROM that you own; the ROM and generated game data are not included.

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

The [Steam Frame guide](docs/STEAM_FRAME.md) has the build prerequisites, APK install steps, controls and settings. The [architecture notes](docs/VR_ARCHITECTURE.md) explain how the port works. The Quest README remains available in [README_QUEST.md](README_QUEST.md).

## Build and install

Build on a desktop running x86_64 Linux, macOS, or Windows through WSL2. On Apple Silicon, install Rosetta for the pinned NDK's x86_64 host tools. The Frame itself runs ARM64 SteamOS, so it cannot run this project's pinned Android NDK build tools. [Android's NDK downloads](https://developer.android.com/ndk/downloads) list the supported desktop hosts.

The public source branch is `steam-frame`:

```sh
mkdir -p ~/src
cd ~/src
git clone --recurse-submodules --branch steam-frame https://github.com/astromaddie/oot-vr.git
cd oot-vr
```

Install JDK 17 and the Android SDK packages listed in the [Steam Frame guide](docs/STEAM_FRAME.md). Generate the source asset pack as described there, then build the `frame` variant:

```sh
cd Android
sh ./gradlew assembleFrame
```

The APK is `Android/app/build/outputs/apk/frame/soh_frame_v9.2.3-android.14-frame.apk`. Building does not install or launch it. The guide covers uploading it with Valve's [SteamOS Devkit Client](https://partner.steamgames.com/doc/steamhardware/steamframe/loadgames) and providing your own game data.

## Credits and licenses

This fork is based on [Ship of Harkinian VR](https://github.com/ChocoTorp/Shipwright-VR) and its upstream engine. This repository has no root license; check the upstream project and component notices before reusing code. The Quest README is preserved for the original port's setup and gameplay details. Game data, extracted assets and saves come from your own copy.
