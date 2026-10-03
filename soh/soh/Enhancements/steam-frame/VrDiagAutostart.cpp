// Steam Frame diagnostics only: get past the title screen with no controller.
//
// A headset on a desk has no controller, so nothing can press Start at the title or pick a file.
// With `autostart=1` in vr-diag.ini (tools/steam_frame/deploy.sh --diag autostart=1) the title
// screen hands over straight to gameplay through the game's own debug-save path, the same one
// the Debug Warp Screen and the "Warp Point" boot sequence use: a temporary file 0xFF (nothing is
// written to any save), the debug inventory, child Link, noon. `autostart_entrance` picks the
// entrance (default 0x00EE, Kokiri Forest).
//
// Compiled only for the Steam Frame build; never touches a save file.
#if defined(SOH_STEAM_FRAME) && defined(__ANDROID__)

#include <spdlog/spdlog.h>
#include <fast/vr_frame_diag.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
void Sram_InitDebugSave(void);
}

static void VrDiagAutostart_Boot(void* gameState) {
    TitleContext* titleContext = (TitleContext*)gameState;
    static bool sDone = false;
    if (sDone) {
        return;
    }
    sDone = true;

    const int entrance = vr_frame_diag().autostart_entrance;
    spdlog::info("[vr-diag] autostart: warping from the title to entrance 0x{:x} with the debug save", entrance);

    gSaveContext.gameMode = GAMEMODE_NORMAL;
    gSaveContext.fileNum = 0xFE; // temporary file so the debug save is respected
    Sram_InitDebugSave();
    gSaveContext.magicFillTarget = gSaveContext.magic;
    gSaveContext.magic = 0;
    gSaveContext.magicCapacity = 0;
    gSaveContext.magicLevel = gSaveContext.magic;
    gSaveContext.fileNum = 0xFF; // not a real file: nothing is saved
    gSaveContext.sceneLayer = 0;
    gSaveContext.cutsceneIndex = 0;
    gSaveContext.linkAge = 0;
    gSaveContext.nightFlag = 0;
    gSaveContext.skyboxTime = gSaveContext.dayTime = 0x8000;
    for (int buttonIndex = 0; buttonIndex < ARRAY_COUNT(gSaveContext.buttonStatus); buttonIndex++) {
        gSaveContext.buttonStatus[buttonIndex] = BTN_ENABLED;
    }
    gSaveContext.nextHudVisibilityMode = gSaveContext.hudVisibilityMode = gSaveContext.hudVisibilityModeTimer = 0;
    gSaveContext.forceRisingButtonAlphas = 0;
    Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_STOP);
    gSaveContext.entranceIndex = entrance;
    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.natureAmbienceId = 0xFF;
    gSaveContext.showTitleCard = true;
    gWeatherMode = 0;
    titleContext->state.running = false;
    SET_NEXT_GAMESTATE(&titleContext->state, Play_Init, PlayState);
    GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);
}

static void RegisterVrDiagAutostart() {
    COND_HOOK(OnZTitleUpdate, vr_frame_diag().autostart, VrDiagAutostart_Boot);
}

static RegisterShipInitFunc sRegisterVrDiagAutostart(RegisterVrDiagAutostart);

#endif // SOH_STEAM_FRAME && __ANDROID__
