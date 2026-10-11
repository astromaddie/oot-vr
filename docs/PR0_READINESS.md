# PR0 — oot-vr

**EXPERIMENTAL.** Kept private while the items below are resolved. See the [README](../README.md) for the port's features, controls, game-data requirements and build instructions.

| Record | Revision |
| --- | --- |
| Hygiene audit | `bfbec4bc8e4c3e290970c72ecbc29a179bf27b5d` |
| Checkout when this record was updated | `bfbec4bc8e4c3e290970c72ecbc29a179bf27b5d` |
| Build/test source | `12d1c9e86c0ece88a5db164f5918e6bdc5f1b202` |

Target: Steam Frame Lepton / Android ARM64 / SteamVR OpenXR.

Canonical host tests passed against libultraship public base c59bbc35edfb50670ce841f13008c475bb05041e plus the recorded patch (SHA-256 f7e92c135e23b0e8d6b90d8f10c06be440b3687568be01afc3960cd4078e959a). Full APK build was not run. No new full application build is claimed for this audit.

The publication blockers are:

- No root source license is present, and attribution for the custom textures is incomplete. The audit found no ROM payload. Resolve the source grant and custom-texture attribution before publication.

User-provided game data is required and is external to this source checkout. Keep the original upstream and component notices; they do not grant rights to the commercial game.

[Exact metadata](PR0_METADATA.json) · [Changelog](../CHANGELOG.md)
