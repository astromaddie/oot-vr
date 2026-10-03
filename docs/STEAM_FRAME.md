# OoT VR on the Valve Steam Frame

The Frame runs Android apps in **Lepton**, an Android 11 (API 30) container with arm64-v8a,
OpenGL ES 3.2 (Mesa Zink on Turnip, Adreno 750) and SteamVR's OpenXR runtime. This port runs the
same Android/OpenXR/multiview build as the Quest one, from a separate Gradle build type, so the
Quest build stays as it was.

Status (2026-10-04): **it builds and the host tests pass. It has not been run on a Frame, not even
unworn.** Everything about how it behaves on the headset is a prediction until someone installs it.
The last section says exactly what is checked and at which level.

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
| System buttons | left menu button opens the VR settings | R Menu = Start, L View = back / recentre, Menu + View = VR settings. See the table below |
| In-headset menu input | laser pointer | laser pointer plus the XR pad as the only gamepad writer for ImGui (see VR_ARCHITECTURE.md) |
| Diagnostics | none | `vr-diag.ini`, `[vr-perf]`, eye dumps, logcat sink |
| Quit | n/a | SteamVR's menu ends the app: the activity is finished when the session goes EXITING |

## Build on a Mac (Apple Silicon)

The Gradle files pin NDK 26.0.10792818 and CMake 3.31.5, so those are what gets used. One-time:

```bash
export JAVA_HOME=/opt/homebrew/opt/openjdk@17 ANDROID_HOME=$HOME/Library/Android/sdk
sdkmanager --sdk_root=$ANDROID_HOME "ndk;26.0.10792818" "cmake;3.31.5" \
    "platforms;android-31" "platforms;android-33" "build-tools;34.0.0"
```

NDK 26's host tools are x86_64, so Rosetta must be installed.

```bash
cd Android && sh ./gradlew assembleFrame     # a clean build is a few minutes to ten; it downloads SDL2, opus, ...
# -> Android/app/build/outputs/apk/frame/soh_frame_v9.2.3-android.14-frame.apk   (debug key)
```

`sh ./gradlew` because the wrapper is not executable in git. Needs about 10 GB of disk (`Android/app/.cxx`
is 7 GB of it).

Things a clean checkout trips over, none of them Frame-specific:

- `SkipPullMasterSword.cpp` called `Camera_RequestMode`, which this tree does not have. Fixed in this branch
  (it is `Camera_ChangeMode` here); no APK could be built without it.
- The manifest asks for `@mipmap/ic_launcher`, but the icon PNGs are not in git (`*.png` is ignored) and
  there is no other mipmap resource. The Frame build type carries its own adaptive icon in `src/frame/res`.
  A clean `assembleDebug` / `assembleRelease` still stops in resource linking for that reason.
- **`soh.o2r` has to be put in the APK by hand.** It holds the port's own assets (fonts, shaders, the custom
  textures), needs no ROM, and is generated on a host by the `GenerateSohOtr` target (a host-built ZAPD does the
  packing). The Quest releases bundle it from `Android/app/src/main/assets/soh.o2r`, which git ignores (`*.o2r`),
  so it is not committed. To make it on a Mac:

  ```bash
  brew install ninja glew libzip nlohmann-json tinyxml2 opus opusfile sdl2_net sdl2   # whatever of these you lack
  cmake -S . -B /tmp/soh-host -G Ninja -DBUILD_REMOTE_CONTROL=1
  cmake --build /tmp/soh-host --target GenerateSohOtr
  cp soh/soh.o2r Android/app/src/main/assets/soh.o2r      # (the target also copies it to the repo root); then assembleFrame
  ```

  Without it the game starts with no HUD assets.

## Deploy

```bash
tools/steam_frame/deploy.sh --rom "/path/to/oot.z64" --register-title   # once
tools/steam_frame/deploy.sh                                              # install + start over adb
```

`FRAME_HOST` (default `steamos@192.168.1.31`) and `FRAME_ADB_HOST` override the targets. adb needs
**Lepton Development** running (start it from the Frame's Steam library; its adbd is on `<frame>:5555`). It
deletes its apps when it exits, so to play use the persistent **OoTVR** title in the Steam library (a Lepton
devkit title, `~/devkit-game/OoTVR/OoT-VR.apk`; every later `deploy.sh` refreshes that copy). A binary started
over adb gets no controllers and no focus from SteamVR: use it for logs and launch the Steam title to play.

SteamVR runs one scene app at a time, and the headset is shared by several sessions. `deploy.sh` books the Frame
through `~/.claude/skills/steam-frame-vr-port/scripts/frame-lock.sh` (session name `FRAME_LOCK_SESSION`, default
`code-fe`) before it installs, launches, registers a title or writes `vr-diag.ini`, stops with the holder's status if
the Frame is booked, and checks for a running Steam title or recent `[openxr]` journal lines before launching.
`--release` gives the booking back. Read-only steps (`--logcat`/`--pull-dumps` on an existing run, `--ssh-log`) need no
booking. adb goes to Lepton Development over Wi-Fi: `FRAME_ADB_HOST` defaults to `192.168.1.31:5555`.

The ROM goes into `~/Documents/OoTVR` (any file name; the game finds it by content, like on the Quest). On the
first start it builds `oot.o2r` from it, which takes a minute or two with only loading dots in the headset. Settings
(`shipofharkinian.json`), saves (`Save/`), `mods/`, `vr-diag.ini` and the game log (`logs/`) land in the same folder.
`deploy.sh --help` lists the rest.

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
| System button | never bound |

The classic (non-selector) profile and the ocarina profile are the Quest ones, except that R Menu is Start in all
three and the left stick click no longer carries Start in the classic one. Everything is still rebindable in
VR Settings > VR Inputs, which still uses the Touch names ("L Menu" there does nothing on the Frame).

What was removed because it collides with the system layer: the optional extra "settings menu button" (a stick
click) is ignored on the Frame profile, and the left menu button, which does not exist there, is no longer the
settings menu's only door. Both-grips for the sword-and-shield swap stays: it is a gameplay chord on a button each
hand owns, not a system one.

On a Touch or Index controller in the Frame build the Quest behaviour is as before (left menu button opens the
settings; Index has no menu button, so Y pauses).

## Unattended testing (nothing worn)

A headset on a desk reports `shouldRender=false`. By default the Frame build ends that frame empty at once.
Frame-only switches live in `~/Documents/OoTVR/vr-diag.ini` (`deploy.sh --diag KEY=VAL`; the architecture doc lists
them with their effects): `force_render`, `diag_yaw`, `dump_frame`, `timing`, `autostart`, `autostart_entrance`,
`stereo_divisor` (1 to 4, overrides `gVrStereoDivisor` for the run, to measure 1 against 2).
With `force_render` the game draws and submits from a synthetic head at 1.6 m (eyes +-32 mm) with the hands parked,
and keeps being stepped. `autostart` skips the title and file select through the game's debug-save path (no save
file is touched) and lands in Kokiri Forest. The dumps are `vr-dumps/vr-eye0.bmp`, `vr-eye1.bmp` and `vr-state.txt`.
Remove the file (`--diag-reset`) before playing.

```bash
tools/steam_frame/deploy.sh --clear-dumps --diag force_render=1 --diag autostart=1 --diag dump_frame=600 \
    --diag diag_yaw=0 --logcat 90 --pull-dumps scratchpad/dumps/yaw0
tools/steam_frame/tests/run_tests.sh      # host tests: bindings, system layer, diag parser, [vr-perf] format
```

`--logcat 90` captures the run to `scratchpad/frame-logcat.txt` (git-ignored). Evidence lines to look for:
`[vr] OpenXR runtime:`, `[vr] Session state`, `Stereo (multiview) swapchain: ... arraySize=2 multiview=1`,
`[vr] ... framebuffers COMPLETE`, `[vr] Display period`, `[VR_CTRL] xrSuggestInteractionProfileBindings ... ok`,
`[VR_CTRL] interaction profile`, `[vr-diag]`, `[vr-perf]`, `[vr-dump]`, `[vr-sys]`, `setupFiles`, `[Setup]`.
Use `deploy.sh --ssh-log 80` to read the same lines from the game's own log file without adb.

## Verified, and at which level

Nothing here has touched a Frame. "Host" means the Mac.

| Claim | Level |
| --- | --- |
| `assembleFrame` builds from a clean checkout plus the fixes in this branch (NDK 26.0.10792818, CMake 3.31.5), APK about 66 MB with soh.o2r | host build |
| The APK's merged manifest has no headtracking feature, no Quest metadata, minSdk 24; the Khronos loader is in it; the Frame strings are in `libsoh.so` | host (inspection of the build output) |
| The Frame-only C/C++ compiles with `-Wall -Wextra` with no warning in the new code, and the files that are shared with the Quest compile without `SOH_STEAM_FRAME` too | host (syntax-only runs with the build's own flags) |
| The Quest debug build still configures and compiles natively (`externalNativeBuildDebug`) | host build (see the note below on what `assembleDebug` does) |
| Binding table: real component paths per profile, Frame layout, unique action and localized names, Frame profile first | host test (`check_bindings.py`, source text only) |
| System layer: View short / 1 s / 3 s, chord, R Menu held back, focus loss | host test (production file, fake clock) |
| `vr-diag.ini` parser, synthetic head, BMP writer, `[vr-perf]` line format | host test |
| `deploy.sh` control flow, including the booking refusing when someone else holds the Frame | host, with stand-ins for ssh, scp, adb and a temporary lock directory |
| Installs in Lepton, SDL activity starts, ROM found in `~/Documents/OoTVR`, `oot.o2r` built | **not run** |
| Session, 1728x1728 sRGB swapchain with `arraySize=2`, multiview FBO complete, 72 Hz loop | **not run** |
| `force_render`, dumps, `[vr-perf]`, `autostart` | **not run** |
| Frame controller profile accepted by SteamVR; button layout as bound here | **not run**; the paths are the ones GoldenEye, Halo and F-Zero bind, which have been worn |
| System layer, recentre, VR settings chord, in-headset menu pad input, Quit from SteamVR | **not run** |
| Anything about what the eyes see, comfort, performance, audio | **not verified (nothing worn)** |

`assembleDebug` is not the check for the Quest build: it fails in resource linking on a clean checkout for the
missing launcher icon, with or without these changes (see above). The native part is what the changes touch.

## Open problems / next

- First thing to run on a Frame: install, `--diag force_render=1 --diag autostart=1 --diag dump_frame=600`, read the
  evidence lines above, look at the dumps.
- `soh.o2r` has to be generated and dropped in before `assembleFrame` (see Build); a built-in way to do it is not wired up.
- The world is redrawn every second XR frame by default (`gVrStereoDivisor` = 2), so 36 Hz of new eye images at 72 Hz,
  reprojected in between. Valve's floor is 72 fps at 1728x1728; whether this counts is a question for a wear session.
- Focus loss does not pause the game or audio yet (standard section 0); input just goes inactive.
- Depth submission, foveation, the Frame render models: not done.
- The snap turn default is 45 degrees (the Quest port's), the standard suggests 30. Not touched.
- The VR Inputs editor and the settings text still use Touch button names.
- `autostart` lands in Kokiri Forest with the debug save and child Link; other scenes need `autostart_entrance`.
