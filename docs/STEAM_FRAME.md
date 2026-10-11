# OoT VR on the Valve Steam Frame

The Frame runs Android apps in **Lepton**, an Android 11 (API 30) container with arm64-v8a,
OpenGL ES 3.2 (Mesa Zink on Turnip, Adreno 750) and SteamVR's OpenXR runtime. This port runs the
same Android/OpenXR/multiview build as the Quest one, from a separate Gradle build type, so the
Quest build stays as it was.

The Frame variant adds persistent game storage, Frame controller bindings, laser navigation, a VR
settings menu, and cinema options for cutscenes and pre-rendered rooms. The controls and setup steps
below apply to the Frame build. See the architecture notes for implementation details.

## What differs from the Quest build

Everything Frame-specific sits behind the Gradle build type `frame`, which sets `BuildConfig.STEAM_FRAME`
and passes `-DSOH_STEAM_FRAME=ON` to CMake (a C/C++ define of the same name).

| Area | Quest (`assembleDebug` / `assembleRelease`) | Frame (`assembleFrame`) |
| --- | --- | --- |
| Application id | `com.linkzenic.soh` | `com.linkzenic.soh.frame` (label "OoT VR") |
| Native flags | debug flags | the same, plus `-DSOH_STEAM_FRAME=ON` |
| OpenXR loader | Khronos 1.1.63 through prefab | the same. Lepton has no loader broker; the loader finds SteamVR through `/vendor/etc/openxr/1/active_runtime.json` |
| Manifest | `src/main` | `src/main` + `src/frame/AndroidManifest.xml`: no required `android.hardware.vr.headtracking` (Lepton does not advertise it, so the install would be refused), no `com.oculus.supportedDevices`, no `focusaware`. minSdk is 24, below Lepton's API 30 |
| Data folder (ROM, `oot.o2r`, saves, settings, mods) | `/storage/emulated/0/SOHVR` | `/sdcard/Documents/OoTVR` = the headset's `~/Documents/OoTVR`, which survives Lepton resets |
| Storage permission | all-files access via a settings screen | not asked for: Lepton 2.8.14 maps `/sdcard/Documents` to the host folder and needs none. The Frame build checks the folder is writable instead, and falls back to the app's own files dir (with a log line) if it is not |
| SDL clipboard | needs the clipboard service | null-safe for everyone (Lepton has no clipboard service) |
| Controllers | Touch (plus Index, simple) | `valve/frame_controller_valve` first when `XR_VALVE_frame_controller_interaction` is advertised, then Index, Touch, simple |
| Physical Menu and system buttons | left menu button opens the VR settings | Right controller's physical Menu = Start, L View = back / recentre, Menu + View = VR settings; the SteamVR system/dashboard button is not bound. See the table below |
| In-headset menu input | laser pointer | laser pointer plus the XR pad as the only gamepad writer for ImGui (see VR_ARCHITECTURE.md) |
| Diagnostics | none | `vr-diag.ini`, `[vr-perf]`, eye dumps, logcat sink |
| Cutscenes | the 6DoF world (immersive) | **3D cinema** by default: the game's own camera on a world-locked 3.6 m screen 3 m ahead, in stereo; VR Settings > HUD & Menus > Cutscenes = Immersive keeps the old way |
| Pre-rendered rooms (Link's house, shops, Market, Temple of Time) | the bare depth mesh (green) | the original backdrop and camera on the cinema screen, one image for both eyes, unless a mod replaces the scene's rooms (Djipi's 3DS pack): then 6DoF as before |
| Pause | the panel with the last stereo frame behind it | the panel alone, with its own frozen world as the pause backdrop |
| HUD, text | head-locked (rolls with the head) | follows head yaw and pitch, never roll |
| Mode changes | none | 0.25 s black, 0.35 s fade-in on every immersive / cinema / panel change |
| Quit | n/a | SteamVR's menu ends the app: the activity is finished when the session goes EXITING |

## Build the APK

Build on an x86-64 Linux or macOS desktop, or on Windows through WSL2 with an x86-64 Ubuntu distribution. These
steps use Bash for the host asset pack and Gradle wrapper. On Apple Silicon, install Rosetta because the pinned
NDK 26 package uses x86-64 host tools. Steam Frame is ARM64 and is not a host for these Android build tools.
Android's [NDK downloads](https://developer.android.com/ndk/downloads) list the available host packages.

### Windows with WSL2

Install [WSL 2 and Ubuntu](https://learn.microsoft.com/en-us/windows/wsl/install). From PowerShell run as
Administrator, install Ubuntu with:

```powershell
wsl --install -d Ubuntu
```

Restart Windows if prompted, open Ubuntu, and create its Linux user. Do the build in Ubuntu, with the checkout and
Android SDK under Linux `~` (for example `~/src`), not under `/mnt/c`. Install the Linux JDK and tools inside Ubuntu;
a Windows JDK or Android SDK is not used by the WSL build:

```bash
sudo apt update
sudo apt install -y openjdk-17-jdk git curl unzip zip openssh-client
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
export ANDROID_HOME="$HOME/Android/Sdk"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"
mkdir -p "$ANDROID_HOME/cmdline-tools/latest" /tmp/android-cmdline-tools
curl -fL -o /tmp/commandlinetools-linux.zip \
  https://dl.google.com/android/repository/commandlinetools-linux-15859902_latest.zip
unzip -q /tmp/commandlinetools-linux.zip -d /tmp/android-cmdline-tools
mv /tmp/android-cmdline-tools/cmdline-tools/* "$ANDROID_HOME/cmdline-tools/latest/"
```

The Linux command-line tools archive and `sdkmanager` are documented on Google's [Android Studio downloads](https://developer.android.com/studio)
and [`sdkmanager` pages](https://developer.android.com/tools/sdkmanager). Keep this Ubuntu terminal open so the
environment variables remain set.

Clone the public Frame branch into the Linux home directory:

```bash
mkdir -p ~/src && cd ~/src
git clone --recurse-submodules --branch steam-frame https://github.com/astromaddie/oot-vr.git
cd oot-vr
```

Install JDK 17, Android SDK command-line tools, Git, CMake and Ninja. The native asset step also needs the host
libraries listed in [BUILDING.md](BUILDING.md) for your operating system. For Debian/Ubuntu Linux and WSL2, install
these tools and the native asset dependencies from the repository root using the [Linux build package list](BUILDING.md#linux):

```bash
sudo apt install -y openjdk-17-jdk git curl unzip zip build-essential $(cat linux-build-deps/apt.txt)
```

On macOS, install JDK 17 and the native asset-pack tools and libraries with Homebrew:

```bash
brew install openjdk@17 cmake ninja glew libzip nlohmann-json tinyxml2 opus opusfile sdl2_net sdl2
```

Leave about 10 GB free for a full Android build (`Android/app/.cxx` uses most of it).

For Linux/macOS only, set these variables to the JDK 17 and Android SDK locations for that host (skip this block if
you followed the Windows/WSL setup above):

```bash
export JAVA_HOME="/path/to/jdk-17"
export ANDROID_HOME="/path/to/android-sdk"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"
```

Accept the Android SDK licenses, then install the pinned packages:

```bash
sdkmanager --sdk_root="$ANDROID_HOME" --licenses
sdkmanager --sdk_root="$ANDROID_HOME" \
  "platform-tools" "platforms;android-31" "platforms;android-33" \
  "build-tools;34.0.0" "ndk;26.0.10792818" "cmake;3.31.5"
export PATH="$ANDROID_HOME/cmake/3.31.5/bin:$PATH"
```

For Linux and macOS, install the command-line tools for that host from Google's [Android Studio downloads](https://developer.android.com/studio)
page and put their `cmdline-tools` contents under `<android-sdk>/cmdline-tools/latest` before running the setup above.

On Windows through WSL2, clone from Ubuntu into `~/src` and run the Linux dependency and SDK commands in that same
Ubuntu environment. A Windows file such as `C:\Users\WindowsUser\Documents\oot.z64` appears there as
`/mnt/c/Users/WindowsUser/Documents/oot.z64`; replace `WindowsUser` with the Windows account name. The Frame game
still reads its ROM from `~/Documents/OoTVR` on the headset.

Build the source asset pack before the APK. `soh.o2r` contains the port's fonts, shaders and textures; it is built
from this repository and does not need a ROM. Gradle does not generate it for you.

```bash
cmake -S . -B /tmp/soh-host -G Ninja -DBUILD_REMOTE_CONTROL=1
cmake --build /tmp/soh-host --target GenerateSohOtr
cp soh/soh.o2r Android/app/src/main/assets/soh.o2r

cd Android
sh ./gradlew assembleFrame
```

With the default `GIT_TAG` unset, the APK is `Android/app/build/outputs/apk/frame/soh_frame_v9.2.3-android.14-frame.apk` and uses the debug key. Set `GIT_TAG` only if you want to change the APK filename.
The game needs a separate compatible Ocarina of Time ROM on the Frame; it is not included in the APK or repository.

## Install and run

If Steam's Devkit Client is running on Windows, copy the built APK from WSL to a Windows folder so the client can
browse it. From the repository root in Ubuntu:

```bash
windows_user="$(cmd.exe /C echo %USERNAME% | tr -d '\r')"
mkdir -p "/mnt/c/Users/$windows_user/Downloads/OoTVR"
cp Android/app/build/outputs/apk/frame/soh_frame_v9.2.3-android.14-frame.apk \
  "/mnt/c/Users/$windows_user/Downloads/OoTVR/"
```

Use Valve's [SteamOS Devkit Client](https://partner.steamgames.com/doc/steamhardware/steamframe/loadgames) from Steam
on your desktop. Pair it once with the Frame: enable Developer Mode, choose **Settings > Developer > Pair new host**,
then register the Frame in the client's **Devkits** tab and confirm pairing on the headset.

For a first install, open **Devkits > Connect to devkit by IP**, choose **Title Upload**, and set:

- **Name:** `OoTVR`
- **Local Folder:** The folder containing the APK: the Windows Downloads folder copied above on WSL, or
  `Android/app/build/outputs/apk/frame` on Linux/macOS.
- **Start Command:** `soh_frame_v9.2.3-android.14-frame.apk`
- **Runtime:** `Android`

Upload and start the title. It then appears in the Frame's Steam library under **Library > Non-Steam > Devkit Game**.
For later builds, upload again with the same title name to update that library entry. Steam launches provide the
Frame controller input.

Put your own compatible Ocarina of Time ROM in `~/Documents/OoTVR` on the Frame. On first launch, the game creates
`oot.o2r` from that ROM in the same folder; saves, settings and mods are stored there too. This generated `oot.o2r`
is different from `soh.o2r`, the source asset pack required during the APK build.

To copy a ROM from your build computer, enable Developer Mode and set the SteamOS password using Valve's
[Steam Frame setup guide](https://partner.steamgames.com/doc/steamhardware/steamframe/setup). Valve's
[debugging guide](https://partner.steamgames.com/doc/steamhardware/steamframe/debugging) documents SSH as
`steamos@frame`. Set `FRAME_HOST` to `frame` or the Frame's IP address; enter the password you set when SSH asks.
These commands use the host's OpenSSH `ssh` and `scp` clients; install `openssh-client` on Ubuntu (included in the
WSL setup above).
From WSL, a ROM saved in Windows Documents is at `/mnt/c/Users/WindowsUser/Documents/oot.z64`; replace
`WindowsUser` with your Windows account name. From the build computer, run:

```bash
export FRAME_HOST=frame  # replace with the Frame's IP if its hostname does not resolve
ROM="/mnt/c/Users/WindowsUser/Documents/oot.z64"  # use the local file path on Linux/macOS
ssh "steamos@$FRAME_HOST" 'mkdir -p /home/steamos/Documents/OoTVR'
scp "$ROM" "steamos@$FRAME_HOST:/home/steamos/Documents/OoTVR/"
```

The ROM may have any filename; the game identifies a compatible image by its contents.

## Controls

The Frame is a split gamepad: **A, B, X, Y and Menu are on the right controller**, View and the D-pad are on the
left, each side has a trigger, grip, bumper and stick. The Quest layout is kept as it was: the Frame's X and Y stand in
for the Quest's left X and Y, so motion combat, the item selector and the ocarina map behave the same.

| Physical | Game (default item-selector profile) |
| --- | --- |
| R Menu | **Start** (pause), on release, and only if View was not touched while Menu was down |
| L View, short press | Back (B) in the 2D menus (file select, pause). Nothing in play: OoT has no Select |
| L View, hold 1 s | Recentre, 0.6 / 80 ms buzz on both hands |
| L View, hold 3 s | Recentre and re-measure the standing height, buzz again |
| Menu + View, held 0.5 s | Opens and closes the in-headset VR settings menu (hold View, then press Menu) |
| Triggers | Use the item held in that hand (selector profile). Pointing: the menu laser clicks with either |
| L grip / R grip | Z-target / R (shield) |
| A / B | A / B |
| X | unbound (selector profile) |
| Y | Start (as on the Quest) |
| L stick | Move. Click: L |
| R stick | Turn (snap 45 degrees by default, smooth in the settings). Click: the item selector (hold, flick, release) |
| D-pad | In play: the N64 D-pad (D-pad equips, ocarina). In the 2D menus: steers the control stick |
| L bumper / R bumper | L / R. In the 2D menus the left one is Z, so Z and R page the pause screen |
| SteamVR system/dashboard button | never bound |

The classic (non-selector) profile and the ocarina profile are the Quest ones, except that R Menu is Start in all
three and the left stick click no longer carries Start in the classic one. Everything is still rebindable in
VR Settings > VR Inputs, which still uses the Touch names ("L Menu" there does nothing on the Frame).

What was removed because it collides with the system layer: the optional extra "settings menu button" (a stick
click) is ignored on the Frame profile, and the left menu button, which does not exist there, is no longer the
settings menu's only door. Both-grips for the sword-and-shield swap stays: it is a gameplay chord on a button each
hand owns, not a system one.

On a Touch or Index controller in the Frame build the Quest behaviour is as before (left menu button opens the
settings; Index has no menu button, so Y pauses).

## Settings added for the Frame

VR Settings > HUD & Menus:

| Setting | CVar | Default | What |
| --- | --- | --- | --- |
| Cutscenes | `gVrCutsceneMode` | 3D Cinema (0) | 3D Cinema or Immersive (1, the old 6DoF way; camera cuts are now shown as cuts there) |
| Pre-Rendered Rooms On The Cinema Screen | `gVrPrerenderCinema` | on | off = the old green 3D view in rooms no mod replaces |
| Cinema 3D Depth | `gVrCinemaDepth` | 0.55 | 0 flat, 1 = far distance at infinity |
| Pause Over The World | `gVrPauseWorld` | on | the pause pages float in front of the frozen 3D world (dimmed, `gVrPauseDim` 0.35); off = the pages alone on black |
| Cinema Screen Width / Distance | `gVrCinemaWidth`, `gVrCinemaDistance` | 3.6 m / 3.0 m | |

Not in the menu: `gVrCinemaConvergence` (0 = window stereo at the nearest visible depth, the default; a positive value
pins the zero-parallax distance in game units, as round 1 did with 150).

VR Settings > Display & Performance (standard section 9):

| Setting | CVar | Default | What |
| --- | --- | --- | --- |
| Refresh Rate | `gVrRefreshRate` | 120 Hz | the highest offered rate at or below it is requested (next start) |
| Advise When Too Slow | `gVrRefreshAutoFallback` | on | logs a suggested SteamVR per-app rate after two 10-second windows below 90% of the target |
| Resolution Scale | `gVrResolutionScale` | 1.0 | a multiplier on the 2160x2160 target (which is the runtime's recommendation when that is larger), 0.75-1.5, next start |

## Performance

In Kokiri Forest, the default divisor of 2 recorded 72 fps with no missed frames per 10 seconds, 10.8–11.3 ms of
CPU time and 3.6–4.1 ms of GPU time. At 72 Hz with divisor 1, it recorded 72 fps with zero to one missed frames
per 10 seconds, 12.8 ms of CPU time and 2.5 ms of GPU time. At 90 Hz with divisor 1, it recorded 90 fps with no
missed frames per 10 seconds and about 2.9 ms of game-thread CPU time per frame.
