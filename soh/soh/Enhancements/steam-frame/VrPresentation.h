// Steam Frame presentation hooks for the game's C code (see VrPresentation.cpp).
#pragma once

#include <stdint.h>

#if defined(SOH_STEAM_FRAME) && defined(__ANDROID__)
#define VR_FRAME_PRESENTATION 1

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
// Once per tick from Play_Draw: decide immersive / 3D cinema / flat cinema, hand it to the VR layer.
int32_t VrFrame_UpdatePresentation(struct PlayState* play);
// Pre-rendered rooms in this scene are drawn the original way (no mod replaces its room files).
int32_t VrFrame_PrerenderOriginal(struct PlayState* play);
// This tick shows such a room.
int32_t VrFrame_PrerenderRoomActive(struct PlayState* play);
// The skybox scale for the current presentation.
float VrFrame_SkyboxScale(void);
// [vr-sky] log line on any change of the sky's inputs.
void VrFrame_LogSky(struct PlayState* play);

#ifdef __cplusplus
}
#endif

#endif
