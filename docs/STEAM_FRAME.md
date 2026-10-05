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
| Cutscenes | the 6DoF world (immersive) | **3D cinema** by default: the game's own camera on a world-locked 3.6 m screen 3 m ahead, in stereo; VR Settings > HUD & Menus > Cutscenes = Immersive keeps the old way |
| Pre-rendered rooms (Link's house, shops, Market, Temple of Time) | the bare depth mesh (green) | the original backdrop and camera on the cinema screen, one image for both eyes, unless a mod replaces the scene's rooms (Djipi's 3DS pack): then 6DoF as before |
| Pause | the panel with the last stereo frame behind it | the panel alone, with its own frozen world as the pause backdrop |
| HUD, text | head-locked (rolls with the head) | follows head yaw and pitch, never roll |
| Mode changes | none | 0.25 s black, 0.35 s fade-in on every immersive / cinema / panel change |
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
| Advise When Too Slow | `gVrRefreshAutoFallback` | on | the app cannot change the Frame's rate (SteamVR offers only the title's per-app rate). Two 10 s windows below 90% while worn log once which per-app rate to set |
| Resolution Scale | `gVrResolutionScale` | 1.0 | a multiplier on the 2160x2160 target (which is the runtime's recommendation when that is larger), 0.75-1.5, next start |

## Unattended testing (nothing worn)

A headset on a desk reports `shouldRender=false`. By default the Frame build ends that frame empty at once.
Frame-only switches live in `~/Documents/OoTVR/vr-diag.ini` (`deploy.sh --diag KEY=VAL`; the architecture doc lists
them with their effects): `force_render`, `diag_yaw`, `diag_roll`, `dump_frame`, `dump_at`, `dump_when`, `dump_count`,
`dump_every`, `diag_menu_open`, `refresh_rate`, `resolution_scale`, `timing`, `autostart`, `autostart_entrance`, `autostart_slot`, `autostart_new_game`, `diag_pause_at`,
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

First-wear checks (each its own run; `--clear-dumps` first; seconds count from the first gameplay tick):

```bash
# 1. Link's house the original way (flat cinema): vr-eye0/1 show the same 4:3 backdrop band, vr-state.txt
#    presentation=flat cinema, layers=cinema-projection; log "[vr] scene 0x34: room files from the game"
tools/steam_frame/deploy.sh --diag force_render=1 --diag autostart=1 --diag autostart_entrance=0x00BB \
    --diag dump_at=15 --diag dump_when=flat_cinema --logcat 60 --pull-dumps scratchpad/dumps/house
# 2+3. The new-game intro in 3D cinema (Deku Tree and Navi): a series of six dumps 2 s apart; each eye shows the
#    16:9 band with the sky; the two eyes differ by a small horizontal shift; "[vr] cinema on: 3D cinema (cutscene)"
#    and "[vr-sky]" lines in the log
tools/steam_frame/deploy.sh --diag force_render=1 --diag autostart_new_game=1 --diag dump_at=8 \
    --diag dump_when=cinema3d --diag dump_count=6 --diag dump_every=180 --logcat 120 --pull-dumps scratchpad/dumps/intro
#    the same with VR Settings > Cutscenes = Immersive (shipofharkinian.json "gVrCutsceneMode": 1) and dump_when=immersive
# 4. Pause from the user's save: vr-panel.bmp shows the pause screen over the frozen world; vr-state.txt has no
#    "projection" in layers=, only panel
tools/steam_frame/deploy.sh --diag force_render=1 --diag autostart_slot=1 --diag diag_pause_at=12 \
    --diag dump_at=16 --diag dump_when=panel --logcat 60 --pull-dumps scratchpad/dumps/pause
# 5. HUD with a rolled head: head_quat in vr-state.txt shows the 30 degree roll (0,0,0.259,0.966), the "hud" line
#    says space=local and quat=0,0,0,1 (level), and vr-hud.bmp is upright
tools/steam_frame/deploy.sh --diag force_render=1 --diag autostart_slot=1 --diag diag_roll=30 \
    --diag dump_at=12 --diag dump_when=immersive --logcat 60 --pull-dumps scratchpad/dumps/roll
# 6. The laser pointer on top: vr-menu.bmp shows the menu with the cursor, vr-state.txt ends layers= with
#    "pointer-beam pointer-dot" and a "pointer hand=1 hit=1" line
tools/steam_frame/deploy.sh --diag force_render=1 --diag autostart_slot=1 --diag diag_menu_open=1 \
    --diag dump_at=5 --logcat 60 --pull-dumps scratchpad/dumps/pointer
# 7. Round 2, 2D in front in the 3D cinema: the intro run of 2+3; "[vr] cinema convergence N (nearest visible N" lines;
#    in vr-eye0/1 the dialogue box and text are at the same place in both eyes while every scene feature is shifted
#    right in vr-eye1 relative to vr-eye0 (uncrossed: behind the screen plane), never left
# 8. Round 2, pause over the world: run 4; vr-state.txt "layers=projection panel(over-world)", vr-eye0/1 show the room
#    (Link's house: the flat-cinema band) and vr-panel.bmp the pages on black (black = transparent there). For the 3D
#    world under the pages: --diag autostart=1 --diag diag_pause_at=12 --diag dump_at=16 --diag dump_when=panel
#    (Kokiri Forest; vr-eye0/1 show the village from the synthetic head, not a captured picture)
# 9. 120 Hz / 2160²: the log has "[vr] display: requesting 120 Hz", "[vr] eye size: recommended ..., runtime maximum ..., -> 2160x2160", and
#    [vr-perf] period=8.3ms; vr-state.txt eye=2160x2160. With --diag refresh_rate=90 --diag resolution_scale=0.8 about
#    90 Hz / 1728² for comparison. The rate advice never fires with force_render (it counts worn time only)
```

`--logcat 90` captures the run to `scratchpad/frame-logcat.txt` (git-ignored). Evidence lines to look for:
`[vr] OpenXR runtime:`, `[vr] Session state`, `Stereo (multiview) swapchain: ... arraySize=2 multiview=1`,
`[vr] ... framebuffers COMPLETE`, `[vr] Display period`, `[VR_CTRL] xrSuggestInteractionProfileBindings ... ok`,
`[VR_CTRL] interaction profile`, `[vr-diag]`, `[vr-perf]`, `[vr-dump]`, `[vr-sys]`, `setupFiles`, `[Setup]`.
Use `deploy.sh --ssh-log 80` to read the same lines from the game's own log file without adb.

## Verified, and at which level

"Host" means the Mac. "Unattended device" means the Frame on a desk (Wi-Fi adb, `force_render`), run on 2026-10-04. Nothing has been worn yet.

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
| Installs in Lepton over Wi-Fi adb; SDL activity starts; data root `/storage/emulated/0/Documents/OoTVR` with no permission prompt; the USA 1.0 ROM is found there and `oot.o2r` is built from it in about 60 s on the first start | unattended device |
| OpenXR through the Khronos loader and `/vendor/etc/openxr/1/active_runtime.json`; session reaches SYNCHRONIZED; 1728x1728 `GL_SRGB8_ALPHA8` swapchain, multiview on (`vr-state.txt`) | unattended device |
| `force_render` steps the game with the headset off; `autostart` warps into Kokiri Forest (scene 0x55, entrance 0xEE) with no controller; dumps at yaw 0 and 90 show a world-locked stereo view with the expected disparity (near hands shift more than the village) | unattended device (`scratchpad/dumps/`, not committed) |
| Kokiri Forest, divisor 2 (default): 72.0 fps, 0 missed per 10 s, CPU 10.8-11.3 ms, GPU 3.6-4.1 ms | unattended device (`[vr-perf]`) |
| Kokiri Forest, divisor 1 (both eyes every frame): 72.0 fps, 0-1 missed per 10 s, CPU 12.8 ms of the 13.9 ms period, GPU 2.5 ms. CPU-bound with about 1 ms to spare; heavier scenes are untested | unattended device (`[vr-perf]`) |
| Kokiri Forest at **90 Hz**, divisor 1: 90.0 fps, 0 missed per 10 s, with or without `[vr-wait]`. `[vr-wait]` shows ~2.9 ms of game-thread CPU a frame; ~6 ms of the "eyes" stage is `xrAcquireSwapchainImage` blocking for a free image (pacing, not work). The earlier "CPU-bound at 72 Hz" reading was that wait | unattended device (`[vr-perf]`, `[vr-wait]`, 2026-10-04) |
| The persistent `OoTVR` devkit title is registered and holds the APK | unattended device (registered, **not launched from Steam yet**) |
| Frame controller profile accepted by SteamVR; button layout as bound here | **not run** (needs a Steam launch); the paths are the ones GoldenEye, Halo and F-Zero bind, which have been worn |
| System layer, recentre, VR settings chord, in-headset menu pad input, Quit from SteamVR | **not run** |
| Anything about what the eyes see, comfort, performance, audio | **not verified (nothing worn)** |

`assembleDebug` is not the check for the Quest build: it fails in resource linking on a clean checkout for the
missing launcher icon, with or without these changes (see above). The native part is what the changes touch.

## Open problems / next

- First wear (phase 3): launch **OoTVR** from the Steam library and run the checklist in VR_ARCHITECTURE.md section 9.
- The Frame build now defaults to stereo divisor 1 (both eyes every frame). Measure Hyrule Field and Kakariko at 90 Hz with `--diag timing=3` and check `[vr-wait]` before trusting it everywhere.
- The OpenXR init lines (runtime name, profile suggestions, swapchain) do not reach the log file; only the session-state lines do. Log them after the logger is up.
- `soh.o2r` has to be generated and dropped in before `assembleFrame` (see Build); it is not wired into Gradle.
- The world is redrawn every second XR frame by default (`gVrStereoDivisor` = 2), so 36 Hz of new eye images at 72 Hz,
  reprojected in between. Valve's floor is 72 fps at 1728x1728; whether this counts is a question for a wear session.
- Focus loss does not pause the game or audio yet (standard section 0); input just goes inactive.
- Depth submission, foveation, the Frame render models: not done.
- The snap turn default is 45 degrees (the Quest port's), the standard suggests 30. Not touched.
- The VR Inputs editor and the settings text still use Touch button names.
- `autostart` lands in Kokiri Forest with the debug save and child Link; other scenes need `autostart_entrance`.
