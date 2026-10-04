// Steam Frame diagnostics only: get past the title screen with no controller.
//
// A headset on a desk has no controller, so nothing can press Start at the title or pick a file.
// With `autostart=1` in vr-diag.ini (tools/steam_frame/deploy.sh --diag autostart=1) the title
// screen hands over straight to gameplay through the game's own debug-save path, the same one
// the Debug Warp Screen and the "Warp Point" boot sequence use: a temporary file 0xFF (nothing is
// written to any save), the debug inventory, linkAge 0 (adult Link; the debug save is adult),
// noon. `autostart_entrance` picks the entrance (default 0x00EE, Kokiri Forest).
//
// Two other starts:
//  - `autostart_slot=N` (1..3): load save file N the way the file select does (Sram_OpenSave, the
//    entrance the save resolves to). Nothing is written until the game itself saves.
//  - `autostart_new_game=1`: a fresh file in memory only (file 0xFF, never saved) with the new-game
//    intro: Link's house child spawn (entrance 0x00BB) with cutscene 0xFFF1, which plays the
//    Deku Tree / Navi opening (layer 5 of Link's house, then the Kokiri Forest cutscenes).
//    The "Skip Intro" time saver and rando skip it; this start logs if either is on.
//
// Compiled only for the Steam Frame build; never writes a save file itself.
#if defined(SOH_STEAM_FRAME) && defined(__ANDROID__)

#include <spdlog/spdlog.h>
#include <fast/vr_frame_diag.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/SaveManager.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
void Sram_InitDebugSave(void);
void Sram_InitNewSave(void);
void Sram_OpenSave(void);
}

static void VrDiagAutostart_CommonTail() {
    gSaveContext.respawn[0].entranceIndex = ENTR_LOAD_OPENING;
    gSaveContext.respawnFlag = 0;
    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.natureAmbienceId = 0xFF;
    gSaveContext.showTitleCard = true;
    gSaveContext.timerState = TIMER_STATE_OFF;
    gSaveContext.subTimerState = SUBTIMER_STATE_OFF;
    gSaveContext.eventInf[0] = gSaveContext.eventInf[1] = gSaveContext.eventInf[2] = gSaveContext.eventInf[3] = 0;
    gSaveContext.prevHudVisibilityMode = 0x32;
    gSaveContext.nayrusLoveTimer = 0;
    gSaveContext.healthAccumulator = 0;
    gSaveContext.magicState = MAGIC_STATE_IDLE;
    gSaveContext.prevMagicState = MAGIC_STATE_IDLE;
    gSaveContext.forcedSeqId = NA_BGM_GENERAL_SFX;
    gSaveContext.skyboxTime = 0;
    gSaveContext.nextTransitionType = TRANS_NEXT_TYPE_DEFAULT;
    gSaveContext.nextCutsceneIndex = 0xFFEF;
    gSaveContext.cutsceneTrigger = 0;
    gSaveContext.chamberCutsceneNum = 0;
    gSaveContext.nextDayTime = 0xFFFF;
    gSaveContext.retainWeatherMode = 0;
    for (int buttonIndex = 0; buttonIndex < ARRAY_COUNT(gSaveContext.buttonStatus); buttonIndex++) {
        gSaveContext.buttonStatus[buttonIndex] = BTN_ENABLED;
    }
    gSaveContext.forceRisingButtonAlphas = gSaveContext.nextHudVisibilityMode = gSaveContext.hudVisibilityMode =
        gSaveContext.hudVisibilityModeTimer = gSaveContext.magicCapacity = 0;
    gSaveContext.magicFillTarget = gSaveContext.magic;
    gSaveContext.magic = 0;
    gSaveContext.magicLevel = gSaveContext.magic;
    gSaveContext.naviTimer = 0;
}

// autostart_slot=N: what FileChoose_LoadGame does for file N (1-based here, 0-based in the game).
static bool VrDiagAutostart_LoadSlot(TitleContext* titleContext, int slot) {
    const int fileNum = slot - 1;
    if (!SaveManager::Instance->SaveFile_Exist(fileNum)) {
        spdlog::warn("[vr-diag] autostart_slot={}: no save file {} - using the debug save instead", slot, slot);
        return false;
    }
    spdlog::info("[vr-diag] autostart: loading save file {} (no controller)", slot);
    gSaveContext.fileNum = fileNum;
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    Sram_OpenSave();
    VrDiagAutostart_CommonTail();
    Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_STOP);
    titleContext->state.running = false;
    SET_NEXT_GAMESTATE(&titleContext->state, Play_Init, PlayState);
    GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);
    return true;
}

// autostart_new_game=1: Sram_InitSave without writing the file.
static void VrDiagAutostart_NewGame(TitleContext* titleContext) {
    const bool skip = CVarGetInteger(CVAR_ENHANCEMENT("TimeSavers.SkipCutscene.Intro"), 0) != 0;
    spdlog::info("[vr-diag] autostart: new game in memory (file 0xFF, not saved): entrance 0x{:x}, cutscene 0x{:x}{}",
                 (int)ENTR_LINKS_HOUSE_CHILD_SPAWN, 0xFFF1,
                 skip ? " - WARNING: Skip Intro is on, the intro will not play" : "");
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    gSaveContext.fileNum = 0xFF;
    Sram_InitNewSave();
    gSaveContext.fileNum = 0xFF; // not a real file: nothing is saved
    gSaveContext.entranceIndex = ENTR_LINKS_HOUSE_CHILD_SPAWN;
    gSaveContext.linkAge = LINK_AGE_CHILD;
    gSaveContext.dayTime = 0x6AAB;
    gSaveContext.cutsceneIndex = 0xFFF1;
    gSaveContext.sceneLayer = 0;
    VrDiagAutostart_CommonTail();
    Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_STOP);
    titleContext->state.running = false;
    SET_NEXT_GAMESTATE(&titleContext->state, Play_Init, PlayState);
    GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);
}

static void VrDiagAutostart_Boot(void* gameState) {
    TitleContext* titleContext = (TitleContext*)gameState;
    static bool sDone = false;
    if (sDone) {
        return;
    }
    sDone = true;

    const VrFrameDiag& d = vr_frame_diag();
    if (d.autostart_new_game) {
        VrDiagAutostart_NewGame(titleContext);
        return;
    }
    if (d.autostart_slot > 0 && VrDiagAutostart_LoadSlot(titleContext, d.autostart_slot)) {
        return;
    }

    const int entrance = d.autostart_entrance;
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
    const VrFrameDiag& d = vr_frame_diag();
    COND_HOOK(OnZTitleUpdate, d.autostart || d.autostart_slot > 0 || d.autostart_new_game, VrDiagAutostart_Boot);
}

static RegisterShipInitFunc sRegisterVrDiagAutostart(RegisterVrDiagAutostart);

#endif // SOH_STEAM_FRAME && __ANDROID__
