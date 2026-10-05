# OoT VR on the Valve Steam Frame: architecture and status

Current state, not a log. Build and deploy steps are in [STEAM_FRAME.md](STEAM_FRAME.md); the Quest side of the
port is described in [../QUEST.md](../QUEST.md) and [../CHANGES.md](../CHANGES.md). The conventions are the Steam
Frame port standard (`standard.md`) and the shared `sfvr` library.

First unattended device run: 2026-10-04. First worn the same day; the fixes for that wear (section 10) are
headset-unverified. The status table uses three states: **played in headset**, **verified unattended** (a headset on a
desk, force-render, logcat, dumps) and **not started / not run**.

## 1. Target

Facts as probed by the other Frame ports on a real Frame (SteamVR OpenXR 2.17.10, Lepton 2.8.14). They are not
re-measured here.

| Item | Value |
| --- | --- |
| Container | Lepton, Android 11 (API 30), arm64-v8a, no clipboard service, no loader broker, no `android.hardware.vr.headtracking` |
| GL | GLES 3.2 through Mesa Zink on Turnip (Adreno 750); `GL_OVR_multiview2`, `GL_EXT_sRGB_write_control` |
| Runtime | SteamVR/OpenXR via `/vendor/etc/openxr/1/active_runtime.json`; the app carries the Khronos loader |
| Eyes | recommended 1728x1728 per eye; the port renders the recommended size times an eye size of 2160 (Frame: the runtime's recommendation brought up to 2160 when smaller, never down) times `gVrResolutionScale` (0.75-1.5, default 1.0), clamped to the runtime's maximum; recommended, maximum and chosen are logged as `[vr] eye size: ...`. The swapchain is created at that size and drawn into directly: never rendered larger and scaled down |
| Swapchain | `GL_SRGB8_ALPHA8`, 3 images, written raw (sRGB write conversion off), one 2-layer image for multiview |
| Refresh | the port requests the highest offered rate at or below `gVrRefreshRate` (default 120), and a one-time log line naming the per-app rate to set if frames are not held (section 11). On the Frame only the title's SteamVR per-app rate is offered |
| Controllers | `valve/frame_controller_valve` with `XR_VALVE_frame_controller_interaction` |
| Game | Ship of Harkinian 9.2.3 (OoT decomp), Fast3D GL renderer, 20 Hz game tick with frame interpolation |

## 2. How it fits together

1. **XR host layer** (libultraship, `libultraship/src/fast/vr_openxr.cpp`): instance, session, swapchains, frame loop,
   composition layers, actions, recentre. `vr_physics.cpp` is the held-object sim (sword, shield, bow);
   `vr_interface.cpp` is the C face the game calls (`VR_*`).
2. **Bridge**: the game pushes Link's head as the camera anchor each tick (`z_play.c`), `padmgr.c` turns controller
   state into the N64 pad, `soh/soh/Enhancements/vr-combat/*` does motion combat, the item selector, archery.
3. **Game**: the decomp. The Frame-only additions are behind `SOH_STEAM_FRAME`.

Frame-only files: `vr_frame_diag.{h,cpp}` (vr-diag.ini, synthetic head, BMP writer), `vr_frame.{h,cpp}` (dumps, GPU
timer, `[vr-perf]`), `vr_system.{h,cpp}` (the system layer as plain logic), `third_party/sfvr` (vendored unedited;
`VERSION` is its git SHA), `soh/soh/Enhancements/steam-frame/VrDiagAutostart.cpp`. Hooks in `vr_openxr.cpp`,
`vr_interface.cpp`, `Gui.cpp`, `Fast3dGui.cpp`, `Context.cpp`, `padmgr.c`, `SohMenuVRSettings.cpp`, `OTRGlobals.cpp`
and the Java activity are all `#ifdef SOH_STEAM_FRAME` / `BuildConfig.STEAM_FRAME`, except the null-safe SDL
clipboard.

### Frame protocol

`DrawAndRunGraphicsCommands` (`Fast3dWindow.cpp`) is the only place the XR frame loop runs, once per XR frame:

1. `vr_begin_frame`: `xrPollEvent`, `xrWaitFrame`, `xrBeginFrame`, `xrLocateViews`, `xrSyncActions` and the hand
   poses, then snap/smooth turning, the held-object sim step, and the per-eye matrices.
2. draw (`vr_begin_stereo` ... `vr_end_stereo` for multiview, per-eye `vr_begin_eye`/`vr_end_eye` otherwise, or the
   flat panel for 2D contexts), the HUD quad, the ImGui menu if it is open.
3. `vr_end_frame`: `xrEndFrame` with the layers.

`xrWaitFrame` is the only pacing; the desktop frame limiter is bypassed in VR. The refresh rate used for the game's
timing is derived from `predictedDisplayPeriod`, not assumed.

- `shouldRender=false` (headset off the head, runtime not asking for frames yet): the Frame build ends the frame it
  began with no layers straight away and returns false. The Quest build returns false and leaves the frame open
  (the callers do not call `vr_end_frame` in that case), which is why this is gated.
- `force_render` (vr-diag.ini): draws and submits anyway from a synthetic head. See section 6.
- Session states are logged by name (`[vr] Session state A -> B`). `EXITING` and `LOSS_PENDING` finish the activity
  (and exit the process after 3 s if the SDL thread does not stop). SteamVR's own menu is the quit button; the game
  has no "Quit" row. From then on nothing may start OpenXR again (`vr_apply_mode_request` and `vr_begin_frame` return
  early) and the Frame build never writes `gVrEnabled=0`. The settings are saved first and a save file write in flight is
  waited for, at most 2.5 s. A failed OpenXR start is retried at most every 10 s instead of every tick.
- **Focus loss** does not pause the game or the audio yet. Input goes inactive on its own (actions are not active
  without focus) and the system layer cancels a half-made View press.

### The game tick against the XR frame

Found by reading `graph.c`, `OTRGlobals.cpp` (`Graph_ProcessGfxCommands`, `RunCommands`) and `Fast3dWindow.cpp`.

The game logic runs **once per 20 Hz tick**, on the same thread as the rendering. Each trip around `Graph_ThreadEntry`
does, in order: read the pad (`PadMgr_ThreadEntry`, so **the controller reaches the game at 20 Hz**), run the logic
(`GameState_Update`), build one display list, then call `Graph_ProcessGfxCommands`, which draws it as **several XR
frames**. In VR `GetInterpolationFPS()` returns the headset's refresh rate, so at 72 Hz a tick is followed by 3 or 4
frames (3, 4, 3, 4, 4, ... averaging 3.6), at 90 Hz by 4 or 5.

Each of those frames is one `DrawAndRunGraphicsCommands`, which means its own `xrWaitFrame`/`xrBeginFrame`/`xrEndFrame`,
and **re-runs the same display list** with:

- object matrices **interpolated** between the previous and the current tick (`FrameInterpolation_Interpolate(t)`),
- the camera anchored at Link's head, also interpolated by `t`, plus the **live head pose** from this frame's
  `xrLocateViews`, and the hands at their live poses (so head and hands track at headset rate, the world animates at
  the interpolated rate).

On top of that, the eye pass itself only runs every `gVrStereoDivisor`-th XR frame (default 2). On the frames in between
the eye images from the last pass are resubmitted with the pose and FOV they were drawn with, and the compositor
reprojects them onto the live head pose. So at 72 Hz the default is a new eye image 36 times a second, and the HUD
quad is redrawn once per tick (`gVrHudPerTick`). The game logic costs a whole tick's worth of time on the render thread
in the frame it runs in; `[vr-perf]` shows it as `tick`/`tick_max`.

In short: it re-renders the interpolated last display list per XR frame with a new head pose, it does not run the
game per XR frame.

### Layers submitted

Back to front:

1. The projection layer. Immersive: the multiview image, one array layer per eye, raw physical head pose and the FOV it
   was drawn with. **3D cinema** (Frame): the same swapchain, but each eye's image holds the game camera's picture in a
   16:9 (4:3 for pre-rendered rooms) band of rows, and each view's pose is (screen orientation, live eye position) with
   the FOV that exactly covers the cinema screen rectangle from that eye (`vr_cinema_eye_view`). Black elsewhere.
   Always submitted on the Frame (section 11): behind the pause panel it is the live frozen world, dimmed; behind file
   select a black image (with no projection layer SteamVR shows its own environment). The stale stereo frame round 1
   removed was the "distorted box".
2. Either the **flat panel** (a world-locked 1280x960 quad, 2.4 m wide at 2.2 m, for 2D contexts: file select, pause; it
   during pause it holds only the kaleido pages, alpha-blended over the world, see section 11) or the **HUD quad** (1024x768, 2 m ahead, following head yaw and pitch
   but not roll on the Frame, or pinned to a hand by setting). In cinema the HUD and text are drawn into the picture.
3. On the Frame without `XR_KHR_composition_layer_color_scale_bias`: a black head-locked fade quad.
4. The **settings menu** (ImGui on a 1280x960 quad, 1.6 m wide at 1.3 m, placed in front of the gaze when it opens,
   alpha-blended).
5. Frame, while the settings menu is open: the **laser pointer**, two quads from one 32x32 disc texture: a 4 mm beam
   from the aim pose to the hit point (1 m when it misses), turned about its own axis to face the head, and a 2 cm dot
   on the hit point. Last, so it is on top of every other layer (standard section 3).

`XR_KHR_composition_layer_color_scale_bias` carries the scene-transition fade, the in-wall darkening and (Frame) the
presentation fade: every change between immersive, 3D cinema, flat cinema and the flat panel cuts to black, holds
0.25 s and fades in over 0.35 s (`vr_present_fade_black`). Not submitted: depth.

## 3. Quest to Frame

| Area | Quest | Frame |
| --- | --- | --- |
| Build | `assembleDebug` / `assembleRelease` | `assembleFrame` (`-DSOH_STEAM_FRAME=ON`, `BuildConfig.STEAM_FRAME`), application id `.frame` |
| Manifest | headtracking required, Quest metadata | `src/frame/AndroidManifest.xml` removes both |
| Loader | Khronos from Maven through prefab | the same |
| Data root | `/storage/emulated/0/SOHVR`, all-files access | `/sdcard/Documents/OoTVR`, no settings screen, write probe, fallback to app files with a log line |
| ROM pickup | scans `/storage/emulated/0` | data folder first, then `/sdcard/Documents`, then `/storage/emulated/0` |
| Logging | spdlog file only | plus logcat (tag `SoH`) |
| Standby frames | left open | ended empty at once; `force_render` draws them |
| Refresh | whatever the system gives | logs the offered rates, asks for the highest at or below `gVrRefreshRate` (120), falls back 120 -> 90 -> 72 when too slow while worn |
| Controller profile | Touch, Index, simple | Frame first, then the same three |
| Left/right buttons | X Y + hamburger on the left, A B on the right | ABXY and Menu on the right, View and D-pad on the left, bumpers; X/Y folded into the left hand's bits |
| Pause | stick click (classic) or Y (selector) | R Menu (all profiles), Y as before in the selector profile |
| Settings menu | left menu button (+ optional stick click) | Menu + View held 0.5 s; the extra stick-click option is ignored |
| Recentre | the runtime's own (`ReferenceSpaceChangePending`) | the same, plus View held 1 s: re-creates the LOCAL space at the head's yaw and position |
| ImGui menu input | laser | laser + XR pad; ImGui's SDL gamepad backend is switched to Manual |
| Quit | n/a | `EXITING` finishes the activity |

## 4. Camera, scale, recentre

- World scale is auto: Link's standing eye height divided by the player's measured eye height above the STAGE floor
  (`gVrAutoWorldScale`); `gVrWorldScale` (35 units per metre) is the fallback.
- The first-person view is composed from the live head pose; snap or smooth turning rotates the world about the head
  (`apply_turn`), never the submitted pose.
- Recentre, Frame build, View held 1 s: at the start of the next frame the LOCAL space is re-created with the head's
  yaw and horizontal position as its origin (what the runtime's own recentre does to its LOCAL space). The accumulated
  artificial turn is dropped, roomscale is re-zeroed (at the next frame that locates views), hand history is reset,
  the eye poses cached for resubmission are moved into the new space, and the flat panel and an open settings menu are
  re-placed. Held 3 s: the same, then the standing height (world scale) is measured again. A runtime (SteamVR system)
  recentre makes the port go back to the runtime's LOCAL space at identity, dropping our offset, with the same resets.
  This is host-tested only as far as the timing goes; the space handling has not run.

## 5. Input

Poses: `grip/pose` places the hands and held items; `aim/pose` is the pointing ray. The settings-menu laser is the
raw aim pose, ray along -Z, and weapon aiming uses the same aim ray (checked in `vr_menu_update` and
`vr_get_aim_ray`; there is no grip-based laser to retune).

Profiles suggested (the log prints each `xrSuggestInteractionProfileBindings` result):

| Profile | Needs | Notes |
| --- | --- | --- |
| `valve/frame_controller_valve` | `XR_VALVE_frame_controller_interaction` advertised, enabled by the port | first; see below |
| `oculus/touch_controller` | core | unchanged from the Quest port |
| `valve/index_controller` | core | unchanged; no menu button |
| `khr/simple_controller` | core | unchanged |

`tools/steam_frame/tests/check_bindings.py` checks every suggested path against the profile's component list, the
Frame layout, and that action names and localized names are unique.

Frame bindings:

| Component | Action | Reaches the game as |
| --- | --- | --- |
| `a/click`, `b/click` (right) | `primary`, `secondary` | right hand PRIMARY / SECONDARY (A, B) |
| `x/click`, `y/click` (right) | `frame_x`, `frame_y` | **left hand** PRIMARY / SECONDARY (the Quest's X, Y) |
| `menu/click` (right) | `menu` | right hand MENU, bound to Start by default |
| `view/click` (left) | `frame_view` | the system layer |
| `dpad_*/click` (left) | `frame_dpad_*` | `VR_GetFrameButtons` (D-pad) |
| `bumper/click` (both) | `frame_bumper` | `VR_GetFrameButtons` (bumpers) |
| `trigger/value`, `squeeze/value`, `thumbstick`, `thumbstick/click` (both) | as on the Quest | as on the Quest |
| `grip/pose`, `aim/pose`, `output/haptic` | as on the Quest | as on the Quest |

The pad is sampled once per game tick (section 2), so a tap shorter than 50 ms can fall between two samples. The Back
pulse from a short View press lasts 150 ms for that reason.

### System layer

The timing is sfvr's (`sfvr_view.h`); `vr_system.cpp` adds the chord and the Back pulse; `vr_openxr.cpp` carries out
the effects.

| Input | Effect |
| --- | --- |
| R Menu | game Start, as a tap: fires on release, and only if View was not down at any time while Menu was. It reaches the game as a 150 ms pulse of the right hand's MENU bit (so the VR Inputs binding still applies), and not at all while the settings menu is up. So Menu + View in any order and either release order never pauses or un-pauses the game |
| View short press | Back pulse (150 ms) to the game, which turns it into B in the 2D menus only; ImGui gets it as B (cancel) |
| View hold 1 s | recentre + 0.6/80 ms buzz on both hands (sfvr's `system` haptic) |
| View hold 3 s | recentre + standing height + buzz |
| Menu + View hold 0.5 s | toggles the VR settings menu; the View press is spent, so it does not also recentre or go Back |

### The in-headset menu and the XR pad

On the Frame, Steam shows an idle virtual pad to SDL. ImGui's SDL2 backend then claims `HasGamepad` and writes that
idle state every frame, which in the F-Zero port left a menu fed from OpenXR dead (lesson 46). SoH uses libultraship's
ImGui, so the same trap is there: `Gui::StartFrame` rescans for gamepads whenever `HasGamepad` is missing, and the SDL
device add/remove handler calls `RefreshImGuiGamepads`. In the Frame build, while VR is up, the backend is put in
`Manual` mode with no pads and both of those leave it alone; `FeedMenuNavFromXrPad` feeds D-pad, left stick, A, B (or a
short View press), X, Y and the bumpers as ImGui gamepad keys, and turns on `NavEnableGamepad` while the menu panel is
being drawn. The laser (trigger = click, right stick = scroll) works as before. None of this has run in a headset.

## 6. Diagnostics (Frame build only, never in the menus)

`~/Documents/OoTVR/vr-diag.ini` (`tools/steam_frame/deploy.sh --diag KEY=VAL`):

| Key | Effect |
| --- | --- |
| `force_render` | keeps VR on while the headset is "doffed", draws and submits while `shouldRender=false`, synthetic head 1.6 m up with eyes +-32 mm, runtime FOV (or the Frame's measured one if the runtime gives none), hands parked in front of the chest; the game keeps being stepped |
| `diag_yaw` | the synthetic head's yaw in degrees, positive turns left |
| `dump_frame` | from XR frame N, on the first frame that draws the eyes (a frame that does not keeps it armed): `vr-dumps/vr-eye0.bmp`, `vr-eye1.bmp` (both multiview layers), `vr-state.txt` (poses, FOV, format, IPD, refresh). One blocking readback, on that frame only |
| `timing` | 1: GPU time of the eye pass (`GL_EXT_disjoint_timer_query`, read late, never waited on) in `[vr-perf]`. 2: that plus the `[vr-wait]` lines (below). 3: `[vr-wait]` only, without the GPU timer queries (reading them may itself make the threaded driver sync) |
| `autostart` | the title hands over to gameplay through the debug-save path, no controller, no save file touched |
| `autostart_entrance` | the entrance for `autostart`, default `0x00EE` (Kokiri Forest) |
| `stereo_divisor` | 1 to 4, overrides `gVrStereoDivisor` for the run (default stays 2) so 1 and 2 can be measured on a device |
| `autostart_slot` | 1 to 3: `autostart` loads that save file the way the file select does (falls back to the debug save if it does not exist). The user's slot 1 is in Kokiri Forest |
| `autostart_new_game` | `autostart` starts a fresh file in memory only (file 0xFF, never saved): entrance `0x00BB` with cutscene `0xFFF1`, i.e. the new-game intro (Deku Tree and Navi, the flight through the forest, Link's house). The Skip Intro time saver and rando skip it (logged) |
| `diag_pause_at` | seconds after gameplay starts: presses Start once (through the game's emulated-button hook), when no cutscene or transition runs |
| `diag_roll` | the synthetic head's roll in degrees (positive tilts the head left); the HUD must stay level |
| `dump_at` | seconds after gameplay starts (the first presentation request) before a dump may fire; combines with `dump_frame` |
| `dump_when` | `any` (default), `immersive`, `cinema`, `cinema3d`, `flat_cinema`, `panel`: the dump waits for that presentation |
| `dump_count`, `dump_every` | a series of dumps, `dump_every` XR frames apart (default 90); files 2.. get `-2`, `-3`, ... |
| `refresh_rate` | Hz, overrides `gVrRefreshRate` for the run (the highest offered rate at or below it is requested; logged `[vr] display: requesting N Hz`) |
| `resolution_scale` | 0.75-1.5, overrides `gVrResolutionScale` for the run (logged in `[VR] Eye 0 render resolution`) |
| `diag_menu_open` | opens the VR settings menu 2 s into gameplay; with `force_render` the parked right hand points straight ahead at it (keep `diag_yaw` 0), so a dump shows the pointer (`vr-menu.bmp`, and `pointer-beam pointer-dot` last in `layers=` with the beam pose) |

A dump writes what that frame drew: `vr-eye0.bmp`/`vr-eye1.bmp` (eye pass), `vr-panel.bmp` (flat panel), `vr-hud.bmp`
(HUD quad), and `vr-state.txt` with the poses, the presentation, the layers submitted, the cinema screen pose and both
cinema views (pose + FOV), and the HUD quad's pose. Cinema eye dumps hold the picture as a band across the square image;
view them cropped to the band (`band_y`, `band_h` in `vr-state.txt`), as seen on the 16:9 screen (lesson 50).

Entrances for `autostart_entrance`: `0x00EE` Kokiri Forest (default), `0x0209` Kokiri Forest outside the Deku Tree
(the meadow), `0x0211` Kokiri Forest outside Link's house, `0x00BB` Link's house (child spawn; pre-rendered), `0x05E8`
Kokiri Forest spawn 12 (the Deku sprout cutscene). The debug save is adult Link (`linkAge` 0), so child-only scenes
and layers need `autostart_slot` or `autostart_new_game`.

`[vr-perf]` every 10 s (sfvr's formatter): `fps`, `missed` (a frame later than 1.5 display periods), `cpu`, then
`tick`, `eyes`, `hud`, `other` in ms (CPU, `xrWaitFrame` excluded; `tick` is amortized over the XR frames because it
runs once per game tick), `eye=` the multiview pass for both eyes, `gpu=` or `n/a`, then `wait`, `period`, `ticks`,
`tick_max`, `eye_passes`, `eye_pass_mean`, `not_rendered`. `[vr-wait]` (`timing=2` or `3`, three lines next to each `[vr-perf]`, ms per frame averaged over all frames) says where the game
thread waits: `stages` (wall / game-thread CPU / time inside timed GL and XR calls for each phase of the frame: eye pass
begin, `Interpreter::Run`, end, HUD, `vr_end_frame`), `xr` (wall / thread CPU of each OpenXR call, calls and calls over 0.2 ms
per frame, slowest call, swapchain acquire/release cycles per frame by kind) and `gl` (the GL backend by category: draw,
vertex ring wait/copy, state, uniforms, shader switch/build, texture upload/state, framebuffer bind/clear/blit,
flush/fence, glGet/readback, with the slowest category). Measures only; `libultraship/include/fast/vr_wait.h`.
Other markers: `[vr]`, `[vr-sys]`, `[vr-diag]`, `[vr-dump]`,
`[VR_CTRL]`, `setupFiles`, `[Setup]`.

## 7. Settings key map

The port keeps SoH's own CVar names; the standard's names are for the docs. Keys without a standard counterpart are
not listed.

| Standard key | CVar | Notes |
| --- | --- | --- |
| `turn_mode`, `snap_angle` | `gVrTurnStyle`, `gVrSnapTurnDegrees` | default snap 45 (the standard suggests 30; not changed) |
| `smooth_turn_speed` | `gVrSmoothTurnSpeed` | 120 deg/s |
| `dominant_hand` | `gVrLeftHanded` | |
| `resolution_scale` | `gVrResolutionScale` | Frame: a multiplier on the ~2160 target (0.75-1.5, default 1.0); Quest: times the recommended size. Next start |
| `refresh_rate` | `gVrRefreshRate` | Frame: 72 / 90 / 120 (default) / 144; takes effect at the next start |
| (per-app rate advice) | `gVrRefreshAutoFallback` | Frame, default on |
| (pause over the world) | `gVrPauseWorld`, `gVrPauseDim` | Frame, default on / 0.35 |
| `hud_distance` | `gVrHudDistance` | |
| `height` / `world_scale` | `gVrAutoWorldScale`, `gVrWorldScale` | |
| `force_render`, `diag_yaw`, `dump_frame`, `timing` | `vr-diag.ini` | not CVars |
| `cutscenes` (`3D CINEMA` / `IMMERSIVE`) | `gVrCutsceneMode` | Frame: 0 = 3D cinema (default), 1 = immersive |
| (pre-rendered rooms on the cinema) | `gVrPrerenderCinema` | Frame, default on |
| (cinema depth, size, distance) | `gVrCinemaDepth`, `gVrCinemaWidth`, `gVrCinemaDistance`, `gVrCinemaConvergence` | 0.55, 3.6 m, 3.0 m, 150 game units (the zero-parallax distance; menu-less) |

## 8. Status

| Area | State |
| --- | --- |
| `assembleFrame` on a Mac; Khronos loader and manifest as intended in the APK; Quest debug native build still compiles | host build / host inspection |
| Binding table, system layer logic, diag parser, `[vr-perf]` format, `deploy.sh` flow | host test |
| Data root and ROM pickup in `/sdcard/Documents/OoTVR`, first-run extraction (~60 s) | verified unattended |
| Session, 1728² sRGB swapchain, multiview, 72 Hz loop | verified unattended (OpenXR init lines are missing from the log file) |
| `force_render`, `autostart`, dumps at yaw 0/90, `[vr-perf]`; 72 fps in Kokiri Forest at divisor 2 and 1 | verified unattended |
| Frame profile, X/Y/Menu/View/D-pad/bumpers | built, not run |
| System layer, recentre by View, settings chord | built, host-tested logic only; space handling not run |
| In-headset menu with the XR pad | built, not run |
| Quit from SteamVR | built, not run |
| Worn: scale, comfort, HUD, controls | not started |
| `soh.o2r` bundled in the APK | done by hand (host-built, gitignored); not wired into Gradle |
| Pause game and audio on focus loss, depth submission, foveation, Frame render models | not started |
| Link's house (pre-rendered room) on the flat cinema, the Deku Tree scene's depth in 3D cinema, the VR settings menu | **played in headset** (2026-10-05) |
| First-wear fixes (section 10): 3D cinema cutscenes in general, roll-free HUD, mode fade, cinema decision log, new diag keys | verified unattended (section 10), not judged worn |
| Round 2 (section 11): window-stereo cinema with the 2D overlays in front, pause over the live world, 120 Hz / 2160² with the per-app rate advice | host build, host tests; **headset-unverified** |
| Missing sky in the intro, immersive mode | not explained; `[vr-sky]` log added |
| 3D pause screen (kaleido as stereo geometry) | not started; the pause panel now floats over the live 3D world instead (section 11) |

## 9. First-wear checklist (about 15 minutes)

Launch **OoTVR** from the Steam library (not over adb), headset on, both controllers awake. The first start
after an install may show loading dots for a few seconds.

0. Before the session: in SteamVR's per-app video settings, set the OoTVR title to 120 Hz (and 2160). The app cannot change the rate itself.
1. Title screen and file select: is the menu readable, and does the laser point where the controller points?
   Make a new file. Name entry uses the D-pad or stick plus A.
2. In Kokiri Forest: look around. The world should stay put when you turn your head. Is Link's scale right
   (you are child Link's height)?
3. Walk with the L stick, turn with the R stick (snap 45 degrees). Comfortable?
4. Swing the sword hand at a bush, raise the shield hand, Z-target with the L grip.
5. Tap R Menu: the pause screen opens; tap again to close. Short View in the pause screen goes back.
6. Hold View 1 s: recentre with a buzz on both hands. Hold View, then press Menu, for the VR settings.
   Can you move around that menu with the controller?
7. Enter Link's house or the shop: the original backdrop on the cinema screen, with a fade in and out (with Djipi's
   3DS pack: the pack's 3D room, 6DoF).
8. Watch the opening Deku Tree cutscene: a 3D screen in front of you, the sky behind the tree, no turning of the world.
   Try VR Settings > HUD & Menus > Cutscenes = Immersive too.
9. Quit from the SteamVR menu. Did it close cleanly? Relaunch: does the save load?

Report anything that felt wrong, plus which stereo mode felt smoother if you try both
(VR Settings > Performance > Stereo Render Divisor, 1 vs 2).

## 10. First wear (2026-10-04)

The user wore the Frame build for the first time. Five reports, worst first. Every fix is Frame-only
(`SOH_STEAM_FRAME`, or a setting the Quest build never reads). **Status of all five: headset-unverified** (host build
and host tests only); section 6 and STEAM_FRAME.md list the unattended runs that check each one.

| # | Report | Root cause | Fix |
| --- | --- | --- | --- |
| 1 | Link's house (and the shops, Market, Temple of Time) are pure green flat-shaded polygons | Two SoH enhancements that default on, "Disable 2D Pre-Rendered Scenes" (`Graphics/Disable2DBackgrounds.cpp`: `VB_DRAW_2D_BACKGROUND` forced false, the pre-rendered skybox swapped for the normal sky) and "Disable Fixed Camera" (`Graphics/DisableFixedCamera.cpp`). They exist for a mod that rebuilds these rooms in 3D. Without one, only the room's own polygon list is left: on the N64 it is drawn first and then covered by the 2D backdrop (`z_room.c`, `func_80096680`), it only gives the actors depth | `VrPresentation.cpp`: in VR, a pre-rendered scene (an explicit list: Market, back alley, Temple of Time exterior, the houses, shops, stable, tent, huts, shooting gallery) whose room files no mod provides (archive of `roomList[i].fileName`, `alt/` included) keeps its backdrops and fixed/pivot cameras: both enhancements stand down for it. A tick in such a room (camera `PREREND_FIXED`/`PREREND_PIVOT` or mesh type 1) is shown as **flat cinema**: the game's own frame on the cinema screen, 4:3, one image for both eyes, with the fade. A scene a mod replaces (Djipi's 3DS pack) stays 6DoF and the enhancements work as before. Logged: `[vr] scene 0x..: room files from the game / replaced by a mod (...)`. Setting `gVrPrerenderCinema` |
| 2 | No sky in the Deku Tree / Navi intro cutscene | Not found on the host (no ROM or scene data here). Ruled out: depth (the sky's render mode neither tests nor writes Z), the VR far plane (30000 covers the 100x box). In 3D cinema the sky is drawn by the game's own path, but the 100x scale would have put it beyond the game's far plane | Cinema: `VrFrame_SkyboxScale` fits the box inside the game's `zFar` (about 49x at 12800), so stereo still reads it as far away. Both modes: a `[vr-sky]` line on every change of scene, layer, `skyboxId`, `skyboxDisabled`, `unk_140`, `fogNear` (the full-screen fog filter becomes opaque below 950), `bgCoverAlpha`, cutscene and presentation. `autostart_new_game` plays the intro unattended. **Immersive is unchanged and unexplained** until that log is read |
| 3 | Cutscenes are the 6DoF world: culled scenery behind you, the view snap-turns in ~30 degree steps | The VR view was always the head in the world; in a cutscene far from Link it rode the director camera (`z_play.c`, third-person branch), whose yaw was swept over one 20 Hz tick when the shot changed (`vr_set_camera_yaw`, snap only above 90 degrees): every cut became a 50 ms whip of the world | **3D cinema** (default, `gVrCutsceneMode` 0): the game's own camera (`z_camera.c` keeps its View, the interpreter keeps its projection) split per eye by `vr_cinema_eye_matrix` (two parallel off-axis cameras converged at 150 units; disparity at infinity = 0.55 x IPD on the screen), drawn into a 16:9 band and submitted as a projection layer whose views have the screen's orientation, the live eye positions and the FOV of the screen corners (`vr_cinema_eye_view`, lesson 45). Screen 3.6 m wide, 3 m ahead, yaw-anchored where the head faced at the start, re-placed on recentre. Text and HUD go into the picture. Detection: `csCtx.state`, `Player_InCsMode` with a non-main or cutscene camera (only after 6 ticks, so a chest or door camera does not cost two fades), entrance cutscenes, the title demo; out after 4 calm ticks; reset with each new PlayState; one log line with every condition on each change. **Immersive** (1): unchanged, except that a yaw change of more than 20 degrees in one tick during a cutscene is shown as a cut (`[vr-cam]` lines) |
| 4 | Pause: the world collapses into a flat box with wrong parallax and distorted edges | During pause the eye images were not redrawn; the last stereo frame was resubmitted with its old pose behind the panel (`vr_end_frame`), and the compositor showed it as a window whose edges differ per eye. The pause screen's own backdrop capture (`FB_CopyToFramebuffer` from "fb 0") was black in VR (`Interpreter::CopyFrameBuffer`) | The panel stands alone (no projection layer while it is up). The frame the game captures is drawn into the panel with the game's own camera (`Camera_Update` does not run while paused, so `z_camera.c` keeps the active camera's own view each tick and `Play_Draw` puts it back while paused) and copied from the panel image (`vr_copy_screen_to_framebuffer`, `CopyExternalTargetTo`), so the kaleido pages sit on the frozen world, mono, world-locked. **3D pause (kaleido as stereo geometry): not done.** Its pages are drawn with their own perspective and the inventory cursor and item models with others; making that world-locked stereo is a renderer project of its own, not cheap or robust. No PAUSE setting was added |
| 5 | Dialogue text and the HUD rotate with head roll | The HUD quad was a VIEW-space quad at identity: fully head-locked, roll included | The head-locked HUD is placed each frame in LOCAL space from the head pose with the roll removed (`vr_present_head_locked`: yaw, then pitch, of the gaze). Checked the other head-relative layers: the flat panel, the settings menu and the cinema screen are yaw-only and world-locked; the hand HUD follows the hand by design; the fade quad is uniform black |

Two more rules from the same day (standard sections 3 and 4), also headset-unverified:

| # | Rule | Before | Now |
| --- | --- | --- | --- |
| 6 | The laser pointer is always on top | No beam at all: the only pointer was ImGui's mouse cursor drawn inside the settings menu's own image. The flat panel (pause, file select) has no pointer: the game's 2D menus take the pad only | Beam and dot as the last two layers (`vr_frame_pointer_layers`), over the menu, panel, cinema, HUD and world. `diag_menu_open` + a dump checks it. The flat panel still has no pointer (nothing there takes one) |
| 7 | Reticles and billboards never roll | Billboards (sprites, Navi, the in-world lock-on reticle and arrow, all from `play->billboardMtxF`) faced the game View, which in VR was the head pose with its up vector: they rolled with the head | In VR the game View's up is the world-up vector nearest the head's (`z_camera.c`); the eyes still render from the true head pose, so only what faces the camera changes. The hookshot reticle sits on the surface normal and was already roll-free; there is no head-locked crosshair |

### Unattended device check (2026-10-05, headset on a desk, 72 Hz)

| # | Run | Result |
| --- | --- | --- |
| 1 | `autostart=1 autostart_entrance=0x00BB dump_when=flat_cinema` | **Verified unattended.** Scene 0x34 logs `room files from the game` and `cinema on: flat cinema (pre-rendered room)`. The dump shows Link's house with its painted backdrop, the HUD, and the 4:3 picture in the cinema band (`layers=cinema-projection`). No green polygons |
| 2 | `autostart_new_game=1 dump_at=68 dump_when=cinema3d dump_count=5 dump_every=360` | **Partly verified.** The Deku Tree / Navi shot ("Navi... where art thou?") plays in 3D cinema with its text in the picture, and its top corners show sky. The wide shot where the sky went missing was not captured. `[vr-sky]` in scene 0x55: skyboxId 29, not disabled. Immersive mode was not run |
| 3 | Same intro run | **Verified unattended:** 3D cinema for the intro (the nightmare at the castle gate, Zelda on horseback, the Deku Tree), letterboxed, with a fade from black between modes. The depth of the cinema image was not judged, and nobody has worn it |
| 4 | `autostart_slot=1 diag_pause_at=12 dump_when=panel` | **Verified unattended:** the user's save loads (it starts in Link's house, a pre-rendered room). The pause screen is on the panel, which is the only layer (`layers=panel`), with the frozen room drawn behind the kaleido pages |
| 5 | `autostart_entrance=0x00EE diag_roll=30 dump_when=immersive` | **Verified unattended (pose only):** with the head rolled 30 degrees, the HUD layer pose is level (`hud ... quat=0,0,0,1`). The compositor's view of the HUD can't be dumped |
| 6 | `diag_menu_open=1 dump_at=8` | **Verified unattended (layer order):** `layers=projection hud menu pointer-beam pointer-dot`, with the beam and dot last. Nit: the menu's left column clips "Comfort & Movement" |
| 7 | none | Not checked on the device (no dump shows billboard roll) |

Everything in this section is still **headset-unverified**. The user's saves were byte-identical before and after the runs (backup compared, then removed).

Also: dumps now cover the flat panel and the HUD (`vr-panel.bmp`, `vr-hud.bmp`) and record the layers, so a pause or a
cinema frame can be dumped (`dump_when`), and a series can be taken through a cutscene (`dump_count`, `dump_every`).

### Worn (2026-10-05)

The user wore the round-1 build: **Link's house on the flat cinema is good, the Deku Tree scene in 3D cinema has good
depth, and the VR settings menu works.** Those three are played in headset. Two new reports led to section 11.

## 11. Second wear (2026-10-05) and the display target

All Frame-only, all **headset-unverified** (host build and host tests).

| # | Report | Root cause | Fix |
| --- | --- | --- | --- |
| 1 | In 3D cinema the 2D overlays (cutscene text, the title logo) read behind the 3D scene they cover | The split converged at a fixed 150 game units: everything nearer had crossed disparity, in front of the screen plane, while the 2D draws sit on it (zero disparity) | **Window stereo** (option a). Each frame converges at the nearest visible scene vertex of the frame before (the interpreter tracks the minimum clip w of unclipped vertices in front of the near plane, outside 2D sections; 5% margin; never nearer than the projection's near plane, `vr_cinema_near_w`). It follows nearer content within about three frames and farther content slowly (`vr_cinema_convergence_step`). The whole scene then has uncrossed disparity behind the screen plane, so the 2D draws are always in front and nothing breaks the screen's edges (no window violation, which crossed-disparity 2D, option b, would cause at full-screen fills and letterbox bars). Option c (a separate layer) would show the text in front but still at a different depth from the picture plane. The overlay display list is marked 2D with a new opcode `G_VR2D` (0x4d) in `graph.c`: title logo, title cards, dialogue, HUD, and the A button that `z_parameter.c` draws with its own perspective all get zero disparity. Flat cinema is mono and needs nothing. `gVrCinemaConvergence` > 0 pins the old fixed convergence. Logged: `[vr] cinema convergence N (nearest visible N, near plane N)` |
| 2 | Pause: a white, blobby background, a flat panel in the middle, the room behind the pages squashed | (a) The panel was the only layer, so SteamVR filled the rest with its own environment. (b) The pause backdrop was the game's capture of one frame, at another aspect than the 4:3 panel | **The frozen world in real 3D behind the panel.** No retained display list was needed: the game state is frozen while paused, so `Play_Draw` simply keeps drawing the world (`gVrPauseWorld`, the captured backdrop is skipped) and the eye pass renders it from the live head pose, dimmed to 35% at the compositor (`gVrPauseDim`). Culling follows the head (`Camera_VrFrameApplyHeadView`, since `Camera_Update` does not run while paused); in a pre-rendered room or a cutscene the eyes keep the cinema picture. The overlay list (the kaleido pages) goes alone to the 4:3 panel in front, cleared transparent and alpha-blended, so there is no backdrop in it. A projection layer is always submitted: behind file select it is a black image. `gVrPauseWorld` 0 gives the panel alone on black |
| 3 | Standard section 9: 120 Hz at ~2160² | The port asked for 90 Hz and rendered 1728² | `gVrRefreshRate` (default 120): the highest offered rate at or below it is requested (`vr_refresh_pick`), offered rates logged. **No fallback ladder** (corrected 2026-10-05, found by the Halo port): on the Frame an app cannot change the refresh rate; SteamVR offers only the title's per-app rate. Two consecutive 10 s windows below 90% while worn (shouldRender, focused, not `force_render`) log once which per-app rate to set (`vr_refresh_governor_frame`, `vr_refresh_fallback` picks the suggestion), `[vr] refresh: ... set the OoTVR title's per-app refresh rate in SteamVR to N Hz`. Eye size: the runtime's recommendation brought up to 2160 when smaller (1.0 when SteamVR already recommends 2160, as the user's per-app setting now does), times `gVrResolutionScale` (0.75-1.5, default 1.0), within the runtime's maximum (`vr_eye_size`, logged `[vr] eye size: recommended, maximum, chosen`). Settings in VR Settings > Display & Performance; diag keys `refresh_rate`, `resolution_scale`. `[vr-perf]` and `[vr-wait]` take the period from the runtime, so they work at any rate. **Performance at 120 Hz / 2160² is not measured** |

### Pause crash (device, 2026-10-05) and its fix

The round-2 build crashed about 0.7 s after opening the pause screen (SIGSEGV at `0xabababababababab` in
`KaleidoScope_UpdateCursorSize`, called from `KaleidoScope_Update`). Cause: drawing the world while paused draws Link,
and `PlayerCall_Draw` (`z_player_call.c`) calls `KaleidoScopeCall_LoadPlayer`, which makes the player actor the
current "kaleido area" overlay (on the N64 the two share that memory). `KaleidoScopeCall_Draw`
(`z_kaleido_scope_call.c`) only draws when the kaleido overlay is current, so the pause screen was never drawn, its
vertex buffers (`pauseCtx->cursorVtx` and the rest, allocated by `KaleidoScope_InitVertices` in the draw) were never
set, and the update crashed on the uninitialised pointer (`0xAB` is `__osMalloc`'s fill). Fix: on the Frame,
`KaleidoScopeCall_Draw` makes the kaleido overlay current again before drawing, as `KaleidoScopeCall_Update` does each
tick. SoH's kaleido overlays are empty table entries, so this is bookkeeping only. The pause screen now runs its
original update and draw order, with the world drawn behind it. **Headset-unverified.**

### Pause pages missing (device, 2026-10-05) and their fix

With the crash fixed, the pause panel showed only the overlay elements (button prompts, hearts, rupees): no item, map,
quest or equipment page. Cause: vanilla kaleido draws its pages into `POLY_OPA` (`KaleidoScope_DrawPages` and the
`z_kaleido_*.c` page drawers), not into the overlay list. With the world kept in the eyes, `POLY_OPA` is the eye
pass, where the VR matrices replace the pages' own projection, so they vanished. Fix (`Play_DrawOverlayElements`,
Frame, pause over the world): the kaleido draw is cut out of `POLY_OPA` into a sub-list (`POLY_OPA` branches over it,
so the eyes skip it) and the overlay list calls it at the same point, before the HUD and the text, so the panel pass
draws the pages with their own camera as before. **Headset-unverified.**
