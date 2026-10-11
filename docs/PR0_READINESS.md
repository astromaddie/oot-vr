# PR0 readiness — Ocarina of Time VR

Visibility remains private. No release package or publication is authorized by this readiness record.

## Source and test identity

- Root source revision: c3cb0b61d9f5718f67f886267e55571b700dee14.
- Public libultraship base: commit c59bbc35edfb50670ce841f13008c475bb05041e, tree 392bf6c7f647b7c6a5c51cc1f900a71404c1410a.
- Exact patched libultraship tree required by the tests: fc24a05187dcd1e90f598aa97b456c8b7ffb2863.
- Tracked patch SHA-256: f7e92c135e23b0e8d6b90d8f10c06be440b3687568be01afc3960cd4078e959a.
- Test entrypoint SHA-256: d28505f9d8227fded133542cd9b2705a1d54df116f67c3eb290f6ae55bacab72.
- Preparation script SHA-256: cc733e134abd9a61e06faf8c024a07ebac319982ae28481044e66baa9105428e.
- Focused tests ran against root revision c3cb0b6 plus the patch tree above. Afterward, the local submodule was restored to the clean public base; the private source revision remains reachable from its steam-frame branch.

## Supported target and dependencies

Target: Steam Frame Lepton, Android 11/API 30, arm64-v8a, OpenGL ES 3.2, SteamVR OpenXR. Gradle pins NDK 26.0.10792818 and CMake 3.31.5; the toolchain also needs JDK 17, Android platforms 31 and 33, build-tools 34.0.0, and Rosetta on Apple Silicon. A full clean build uses roughly 10 GB and requires the separately generated ignored soh.o2r asset.

The root repository is private, so no anonymous root checkout is claimed. In an isolated checkout with an empty HOME, no personal Git configuration, and GitHub credentials unset, the public libultraship base was fetched and the preparation script reconstructed the exact target tree. A second preparation run was idempotent. The root did not receive a public-visibility change.

## Verification by level

| Level | Evidence |
|---|---|
| Host | tools/steam_frame/tests/run_tests.sh passed bindings, system, diagnostics, presentation, performance formatting, adjustment, camera, and gamepad suites against the exact patched dependency tree. |
| Unattended Frame | Historical docs record selected boot, OpenXR readiness, scenes, pause/menu and cinema captures. The latest camera-migration work is associated with source 45ae9961201c9887e669db549988bb1b136ad983, but the installed APK came from an earlier dirty source state; it is not a reproducible candidate identity. |
| Worn | Older revisions had selected wearer checks. The current camera migration and integrated source have no focused wearer acceptance. Sustained current-source performance is unmeasured. |

No full clean Frame build was run during PR0. Prior unattended 72/90 Hz numbers belong to earlier captures and selected scenes; they do not establish current-source performance or complete game play.

Known items in the source documentation include scene-specific gaps, untested seated and spatial-audio behavior, manual controller navigation and settings pages still awaiting wear checks, and open Hyrule Field/Kakariko performance measurement. Read the status table in VR_ARCHITECTURE.md for feature-specific distinctions.

## Rights and publication blockers

There is no root license file. Review the upstream/decompilation and media terms before distribution; do not assume that the upstream URL grants rights to all source or game assets. User game data remains external. PR0 leaves the repository private and does not create a release.

## Pending wearer acceptance

For a candidate built from an exact, clean source state, record:

- Install/update and launch from Steam.
- Actual Frame controller input, including the Frame profile and all game/menu actions.
- Tracked pose, recentering, per-eye stereo and alignment in representative scenes.
- Main and VR menus, pause, resume, save, reload, and normal exit.
- Representative gameplay across more than the startup scene; do not mark the game complete from boot or a short scene.
- Worn comfort and sustained frame pacing, measured in a named scene at the tested refresh/resolution.
