# OoT VR on the Valve Steam Frame: architecture and status

Current state, not a log. Build and deploy steps are in [STEAM_FRAME.md](STEAM_FRAME.md); the Quest side of the
port is described in [../QUEST.md](../QUEST.md) and [../CHANGES.md](../CHANGES.md). The conventions are the Steam
Frame port standard (`standard.md`) and the shared `sfvr` library.

Nothing here has run on a Frame (2026-10-04). The status table at the end uses three states: **played in headset**,
**verified unattended** (a headset on a desk, force-render, logcat, dumps) and **not started / not run**. Today
`played in headset` and `verified unattended` are empty; what exists is host evidence, and the table says so.

## 1. Target

Facts as probed by the other Frame ports on a real Frame (SteamVR OpenXR 2.17.10, Lepton 2.8.14). They are not
re-measured here.

| Item | Value |
| --- | --- |
| Container | Lepton, Android 11 (API 30), arm64-v8a, no clipboard service, no loader broker, no `android.hardware.vr.headtracking` |
| GL | GLES 3.2 through Mesa Zink on Turnip (Adreno 750); `GL_OVR_multiview2`, `GL_EXT_sRGB_write_control` |
| Runtime | SteamVR/OpenXR via `/vendor/etc/openxr/1/active_runtime.json`; the app carries the Khronos loader |
| Eyes | recommended 1728x1728 per eye; the port renders the recommended size times `gVrResolutionScale` (default 1.0), clamped to the runtime's maximum |
| Swapchain | `GL_SRGB8_ALPHA8`, 3 images, written raw (sRGB write conversion off), one 2-layer image for multiview |
| Refresh | 72 Hz offered; the port asks for 90 only if the runtime lists it |
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
  has no "Quit" row.
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

Back to front: the projection layer (the multiview image, one array layer per eye, raw physical head pose and the FOV it
was drawn with), then either the **flat panel** (a world-locked 1280x960 quad, 2.4 m wide at 2.2 m, for 2D contexts:
file select, pause; the eye image keeps its last world frame behind it) or the **HUD quad** (1024x768, head-locked at 2 m,
or pinned to a hand by setting), then the **settings menu** (ImGui on a 1280x960 quad, 1.6 m wide at 1.3 m, placed in
front of the gaze when it opens, alpha-blended, on top). `XR_KHR_composition_layer_color_scale_bias` is used for the
scene-transition fade and the in-wall darkening when advertised. Not submitted: depth.

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
| Refresh | whatever the system gives | logs the offered rates, asks for 90 when offered |
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
  artificial turn is dropped, roomscale is re-zeroed, and the flat panel and an open settings menu are re-placed.
  Held 3 s: the same, then the standing height (world scale) is measured again. A runtime recentre is handled as before.
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
| R Menu | game Start. Not delivered while View is down |
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
| `timing` | GPU time of the eye pass (`GL_EXT_disjoint_timer_query`, read late, never waited on) in `[vr-perf]` |
| `autostart` | the title hands over to gameplay through the debug-save path, no controller, no save file touched |
| `autostart_entrance` | the entrance for `autostart`, default `0x00EE` (Kokiri Forest) |

`[vr-perf]` every 10 s (sfvr's formatter): `fps`, `missed` (a frame later than 1.5 display periods), `cpu`, then
`tick`, `eyes`, `hud`, `other` in ms (CPU, `xrWaitFrame` excluded; `tick` is amortized over the XR frames because it
runs once per game tick), `eye=` the multiview pass for both eyes, `gpu=` or `n/a`, then `wait`, `period`, `ticks`,
`tick_max`, `eye_passes`, `eye_pass_mean`, `not_rendered`. Other markers: `[vr]`, `[vr-sys]`, `[vr-diag]`, `[vr-dump]`,
`[VR_CTRL]`, `setupFiles`, `[Setup]`.

## 7. Settings key map

The port keeps SoH's own CVar names; the standard's names are for the docs. Keys without a standard counterpart are
not listed.

| Standard key | CVar | Notes |
| --- | --- | --- |
| `turn_mode`, `snap_angle` | `gVrTurnStyle`, `gVrSnapTurnDegrees` | default snap 45 (the standard suggests 30; not changed) |
| `smooth_turn_speed` | `gVrSmoothTurnSpeed` | 120 deg/s |
| `dominant_hand` | `gVrLeftHanded` | |
| `resolution_scale` | `gVrResolutionScale` | times the recommended eye size; takes effect at the next start |
| `hud_distance` | `gVrHudDistance` | |
| `height` / `world_scale` | `gVrAutoWorldScale`, `gVrWorldScale` | |
| `force_render`, `diag_yaw`, `dump_frame`, `timing` | `vr-diag.ini` | not CVars |

## 8. Status

| Area | State |
| --- | --- |
| `assembleFrame` on a Mac; Khronos loader and manifest as intended in the APK; Quest debug native build still compiles | host build / host inspection |
| Binding table, system layer logic, diag parser, `[vr-perf]` format, `deploy.sh` flow | host test |
| Data root and ROM pickup in `/sdcard/Documents/OoTVR`, first-run extraction | built, not run |
| Session, swapchain, multiview, 72 Hz loop, logging | built, not run |
| `force_render`, `autostart`, dumps, `[vr-perf]` | built, not run |
| Frame profile, X/Y/Menu/View/D-pad/bumpers | built, not run |
| System layer, recentre by View, settings chord | built, host-tested logic only; space handling not run |
| In-headset menu with the XR pad | built, not run |
| Quit from SteamVR | built, not run |
| Worn: scale, comfort, HUD, controls | not started |
| `soh.o2r` bundled in the APK | not done (needs a host-built one) |
| Pause game and audio on focus loss, depth submission, foveation, Frame render models | not started |
