# Ocarina of Time VR for Steam Frame — experimental technical preview

This private Ship of Harkinian VR fork adds a native Steam Frame target through Lepton (Android 11/API 30), arm64-v8a, OpenGL ES 3.2, and SteamVR OpenXR. The Frame build is a separate Gradle variant. This is an experimental port preview; it does not claim complete-gameplay acceptance.

The project is based on [Ship of Harkinian VR](https://github.com/ChocoTorp/Shipwright-VR). General engine changes should be proposed upstream; Frame-specific changes belong in this fork and need owner access. The Quest installation README is preserved in [README_QUEST.md](README_QUEST.md). This repository remains private.

## Capability evidence

| Capability | Host | Frame on desk | Worn |
|---|---|---|---|
| Build and XR startup | Focused host suite passed with public libultraship base plus exact tracked patch; no current Frame APK was built. | Earlier worktree reached OpenXR readiness; its installed APK came from an earlier dirty state. | Current source and camera migration have no focused wear check. |
| Stereo scenes | Host projection, camera and presentation tests passed. | Historical synthetic-head captures checked Kokiri Forest, Link's house/cinema paths, and pause presentation; selected Kokiri runs measured 72/90 Hz. | Older wear sessions covered selected scenes only; no claim covers this source revision. |
| Controllers and menus | Binding/system and gamepad host checks passed. | The Frame interaction profile and real controller path were not exercised in the unattended runs. | Current controller input, manual menu navigation, settings readability and comfort remain open. |
| Saves and gameplay | Save-path logic was not a complete gameplay test. | A prior user save and selected pause/resume flow were exercised. | Current install, play, pause, save/reload, and exit need a focused session. |
| Performance | Host tests are not device timing. | Older Kokiri Forest runs reached 72 and 90 Hz; heavier scenes and this source revision are unmeasured. | Current-source sustained performance is unmeasured. |

See [docs/VR_ARCHITECTURE.md](docs/VR_ARCHITECTURE.md) and [docs/PR0_READINESS.md](docs/PR0_READINESS.md) for source identities and exact evidence levels.

## Reconstruct the Frame build

On Apple Silicon macOS, install JDK 17, Android SDK, NDK 26.0.10792818, CMake 3.31.5, Android platforms 31 and 33, build-tools 34.0.0, and Rosetta. The build requires about 10 GB of disk.

~~~sh
export JAVA_HOME=/opt/homebrew/opt/openjdk@17
export ANDROID_HOME=$HOME/Library/Android/sdk
sdkmanager --sdk_root=$ANDROID_HOME "ndk;26.0.10792818" "cmake;3.31.5" "platforms;android-31" "platforms;android-33" "build-tools;34.0.0"
git submodule update --init --recursive
tools/steam_frame/tests/run_tests.sh
brew install ninja glew libzip nlohmann-json tinyxml2 opus opusfile sdl2_net sdl2
cmake -S . -B /tmp/soh-host -G Ninja -DBUILD_REMOTE_CONTROL=1
cmake --build /tmp/soh-host --target GenerateSohOtr
cp soh/soh.o2r Android/app/src/main/assets/soh.o2r
cd Android
sh ./gradlew assembleFrame
~~~

The submodule URL is public libultraship at a pinned base. The tracked Frame patch and tools/steam_frame/prepare_libultraship.sh reconstruct the exact required tree before Gradle or host tests. The Frame variant needs the generated port asset soh.o2r; the source does not include it. PR0 did not run the full Android build because of disk cost. No ROM is needed to generate soh.o2r, but playing requires the user's own legally obtained compatible game data. No ROM, extracted game data, or saves are included.

## Known limits and rights

The root repository has no license file. Do not infer source, decompilation, or media rights from its upstream relationship. Source/media review remains open, and there is no public release or visibility change. The clean Frame build requires a separately generated, ignored soh.o2r asset. A current-source Frame package and worn test have not been produced.

## PR0 changelog

- Preserved the Quest README and rewrote the root page as a private Steam Frame preview.
- Replaced the private libultraship submodule source with a public pinned base plus an exact, SHA-256-checked tracked patch.
- Added readiness evidence and the pending wearer checklist in docs/PR0_READINESS.md.
