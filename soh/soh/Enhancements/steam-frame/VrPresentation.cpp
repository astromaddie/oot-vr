// Steam Frame: what the eyes show each tick, and pre-rendered rooms drawn the original way.
//
// - Cutscenes go to the 3D cinema (the game's own camera on a world-locked screen, in stereo) unless
//   VR Settings > CUTSCENES is IMMERSIVE (gVrCutsceneMode 1). The decision and its log line are
//   vr_present_decide (libultraship/src/fast/vr_present.cpp, host-tested).
// - Pre-rendered rooms (Link's house, shops, the Market, the Temple of Time exterior...) are 2D
//   backdrops behind a fixed or pivoting camera. SoH's "Disable 2D Pre-Rendered Scenes" and
//   "Disable Fixed Camera" (both on by default, meant for a mod pack that rebuilds these rooms in
//   3D) left only the rooms' untextured depth meshes in VR: the green polygons of the first wear.
//   In VR on the Frame, unless a mod replaces the scene's room files (Djipi's 3DS pack does), those
//   two enhancements stand down for the scene and the room is shown the original way, on the cinema
//   screen with one image for both eyes (gVrPrerenderCinema, default on).
// - diag_pause_at (vr-diag.ini) presses Start once, for unattended pause dumps.
//
// Steam Frame build only. Called from z_play.c, z_camera.c, z_room.c and the two enhancements.
#if defined(SOH_STEAM_FRAME) && defined(__ANDROID__)

#include <cstring>
#include <set>
#include <string>

#include <spdlog/spdlog.h>
#include <fast/vr_frame.h>
#include <fast/vr_frame_diag.h>
#include <fast/vr_present.h>
#include <vr_interface.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/resource/Resource.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/Archive.h>
#include <ship/resource/archive/ArchiveManager.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ResourceManagerHelpers.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "z64camera.h"
extern PlayState* gPlayState;
}

namespace {

// The base archives: anything else that provides a file is a mod.
bool IsBaseArchive(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    return name == "oot.o2r" || name == "oot-mq.o2r" || name == "soh.o2r" || name == "oot.otr" ||
           name == "oot-mq.otr" || name == "soh.otr";
}

// The archive that provides `path` (alt/ first when alternate assets are on) is a mod.
bool FileFromMod(std::string path) {
    if (path.rfind("__OTR__", 0) == 0) {
        path = path.substr(7);
    }
    if (ResourceMgr_IsGameMasterQuest()) {
        const size_t pos = path.find("/nonmq/");
        if (pos != std::string::npos) {
            path.replace(pos, 7, "/mq/");
        }
    }
    auto rm = Ship::Context::GetRawInstance()->GetResourceManager();
    auto am = rm->GetArchiveManager();
    if (rm->IsAltAssetsEnabled()) {
        auto alt = am->GetArchiveFromFile(Ship::IResource::gAltAssetPrefix + path);
        if (alt != nullptr && !IsBaseArchive(alt->GetPath())) {
            return true;
        }
    }
    auto arc = am->GetArchiveFromFile(path);
    return arc != nullptr && !IsBaseArchive(arc->GetPath());
}

// The scenes built from pre-rendered backdrops (2D JFIF rooms or pre-rendered pivot skyboxes).
// Only these are kept the original way: real 3D scenes on SoH's fixed-camera list (Forest Temple,
// Zelda's courtyard) keep "Disable Fixed Camera".
const std::set<s16> kPrerenderedScenes = {
    SCENE_MARKET_ENTRANCE_DAY, SCENE_MARKET_ENTRANCE_NIGHT, SCENE_MARKET_ENTRANCE_RUINS,
    SCENE_BACK_ALLEY_DAY, SCENE_BACK_ALLEY_NIGHT, SCENE_MARKET_DAY, SCENE_MARKET_NIGHT, SCENE_MARKET_RUINS,
    SCENE_TEMPLE_OF_TIME_EXTERIOR_DAY, SCENE_TEMPLE_OF_TIME_EXTERIOR_NIGHT, SCENE_TEMPLE_OF_TIME_EXTERIOR_RUINS,
    SCENE_KNOW_IT_ALL_BROS_HOUSE, SCENE_TWINS_HOUSE, SCENE_MIDOS_HOUSE, SCENE_SARIAS_HOUSE,
    SCENE_BACK_ALLEY_HOUSE, SCENE_BAZAAR, SCENE_KOKIRI_SHOP, SCENE_GORON_SHOP, SCENE_ZORA_SHOP,
    SCENE_POTION_SHOP_KAKARIKO, SCENE_POTION_SHOP_MARKET, SCENE_POTION_SHOP_GRANNY, SCENE_BOMBCHU_SHOP,
    SCENE_HAPPY_MASK_SHOP, SCENE_LINKS_HOUSE, SCENE_DOG_LADY_HOUSE, SCENE_STABLE, SCENE_IMPAS_HOUSE,
    SCENE_KAKARIKO_CENTER_GUEST_HOUSE, SCENE_CARPENTERS_TENT, SCENE_GRAVEKEEPERS_HUT, SCENE_SHOOTING_GALLERY,
};

struct SceneVerdict {
    const PlayState* play = nullptr;
    s16 sceneNum = -1;
    const void* roomList = nullptr;
    bool replaced = false; // a mod provides at least one of the scene's room files
};
SceneVerdict sScene;

bool SceneRoomsReplaced(PlayState* play) {
    if (sScene.play == play && sScene.sceneNum == play->sceneNum && sScene.roomList == play->roomList) {
        return sScene.replaced;
    }
    sScene.play = play;
    sScene.sceneNum = play->sceneNum;
    sScene.roomList = play->roomList;
    sScene.replaced = false;
    std::string which;
    if (play->roomList != nullptr) {
        for (s32 i = 0; i < play->numRooms; i++) {
            const char* f = play->roomList[i].fileName;
            if (f != nullptr && FileFromMod(f)) {
                sScene.replaced = true;
                which = f;
                break;
            }
        }
    }
    spdlog::info("[vr] scene 0x{:x}: room files {}", play->sceneNum,
                 sScene.replaced ? "replaced by a mod (" + which + "): 6DoF, the mod's 3D rooms" : "from the game");
    return sScene.replaced;
}

int sPresentMode = VR_PRESENT_IMMERSIVE;
int sImmersiveTicks = 0; // consecutive ticks that asked to leave the cinema
int sHeldCamTicks = 0;   // consecutive ticks of "Link held + a cutscene camera" with no scripted cutscene
const PlayState* sPresentPlay = nullptr;
std::string sLastWhy;
bool sPauseDiagDone = false;
int sPauseDiagTries = 0;
int sPauseDiagWait = 0;

bool CutsceneCameraSetting(s16 setting) {
    return setting == CAM_SET_CS_0 || setting == CAM_SET_CS_3 || setting == CAM_SET_CS_ATTENTION ||
           setting == CAM_SET_CS_C;
}

} // namespace

// Pre-rendered rooms are drawn the original way in this scene (2D backdrop, fixed or pivot camera).
extern "C" s32 VrFrame_PrerenderOriginal(PlayState* play) {
    if (play == nullptr || !VR_IsInitialized() || !CVarGetInteger("gVrPrerenderCinema", 1) ||
        !kPrerenderedScenes.count(play->sceneNum)) {
        return false;
    }
    return !SceneRoomsReplaced(play);
}

// This tick shows a pre-rendered room drawn the original way.
extern "C" s32 VrFrame_PrerenderRoomActive(PlayState* play) {
    if (!VrFrame_PrerenderOriginal(play)) {
        return false;
    }
    Camera* cam = GET_ACTIVE_CAM(play);
    const bool prerenderCam =
        cam != nullptr && (cam->setting == CAM_SET_PREREND_FIXED || cam->setting == CAM_SET_PREREND_PIVOT);
    const Room* room = &play->roomCtx.curRoom;
    const bool imageRoom = room->segment != nullptr && room->meshHeader != nullptr && room->meshHeader->base.type == 1;
    return prerenderCam || imageRoom;
}

// Once per tick from Play_Draw, before the VR camera anchor: decide and hand the mode to the VR layer.
extern "C" s32 VrFrame_UpdatePresentation(PlayState* play) {
    if (!VR_IsInitialized() || play == nullptr) {
        return VR_PRESENT_IMMERSIVE;
    }
    vr_frame_note_gameplay(); // dump_at / diag_pause_at count from the first gameplay tick
    if (play != sPresentPlay) {
        // A new PlayState (new game, file load, return from the title): nothing carries over.
        sPresentPlay = play;
        sPresentMode = VR_PRESENT_IMMERSIVE;
        sImmersiveTicks = 0;
        sHeldCamTicks = 0;
    }
    Player* player = GET_PLAYER(play);
    Camera* cam = GET_ACTIVE_CAM(play);
    VrPresentInputs in;
    in.cutscene_setting_cinema = CVarGetInteger("gVrCutsceneMode", 0) == 0;
    in.prerender_setting_cinema = CVarGetInteger("gVrPrerenderCinema", 1) != 0;
    in.cs_active = play->csCtx.state != CS_STATE_IDLE;
    in.player_cs_mode = player != nullptr && Player_InCsMode(play);
    in.cam_cutscene = play->activeCamera != CAM_ID_MAIN || (cam != nullptr && CutsceneCameraSetting(cam->setting));
    in.intro_cs = gSaveContext.cutsceneIndex >= 0xFFF0 && in.player_cs_mode;
    in.title_demo = gSaveContext.gameMode == GAMEMODE_TITLE_SCREEN;
    in.prerender_room = VrFrame_PrerenderRoomActive(play);
    in.talking = Message_GetState(&play->msgCtx) != TEXT_STATE_NONE;
    // A short camera with no scripted cutscene (a chest, a door, a switch reveal) only counts once it
    // has lasted 6 ticks (0.3 s): otherwise every one-second camera would be two fades.
    const bool heldCamOnly = !in.cs_active && !in.intro_cs && in.player_cs_mode && in.cam_cutscene;
    sHeldCamTicks = heldCamOnly ? sHeldCamTicks + 1 : 0;
    if (heldCamOnly && sHeldCamTicks < 6 && sPresentMode == VR_PRESENT_IMMERSIVE) {
        in.cam_cutscene = false;
    }

    char why[320];
    const VrPresentMode want = vr_present_decide(in, why, sizeof(why));
    // Into a cinema at once (the fade hides the cut), out of it only after 4 ticks (200 ms) that all
    // ask for it, so a one-tick gap between two cutscene shots does not flash the world.
    if (want != VR_PRESENT_IMMERSIVE || sPresentMode == VR_PRESENT_IMMERSIVE) {
        sImmersiveTicks = 0;
        sPresentMode = want;
    } else if (++sImmersiveTicks >= 4) {
        sImmersiveTicks = 0;
        sPresentMode = want;
    }
    // One line whenever the decision or any of its reasons changes (lesson 48), with every condition.
    const std::string whyNow = std::to_string(sPresentMode) + why;
    if (whyNow != sLastWhy) {
        sLastWhy = whyNow;
        spdlog::info("[vr] cinema {}: {} (scene 0x{:x}, cam setting 0x{:x}, active cam {})",
                     sPresentMode == VR_PRESENT_IMMERSIVE ? "off" : "on", why, play->sceneNum,
                     cam != nullptr ? cam->setting : -1, play->activeCamera);
    }
    VR_SetPresentation(sPresentMode);
    VR_SetCutsceneHint(in.cs_active || in.intro_cs || (in.player_cs_mode && in.cam_cutscene));

    // diag_pause_at: a Start press when gameplay is calm enough to pause, repeated every second (at
    // most 10 times) until the pause screen opens, in case the game ignored one.
    const VrFrameDiag& d = vr_frame_diag();
    if (d.diag_pause_at > 0.0f && !sPauseDiagDone) {
        if (play->pauseCtx.state != 0) {
            sPauseDiagDone = true;
            spdlog::info("[vr-diag] diag_pause_at: the pause screen is open");
        } else if (sPauseDiagWait > 0) {
            sPauseDiagWait--;
        } else if (vr_frame_gameplay_seconds() >= d.diag_pause_at && !Play_InCsMode(play) &&
                   play->transitionTrigger == TRANS_TRIGGER_OFF && play->transitionMode == TRANS_MODE_OFF &&
                   play->msgCtx.msgMode == MSGMODE_NONE) {
            if (++sPauseDiagTries > 10) {
                sPauseDiagDone = true;
                spdlog::warn("[vr-diag] diag_pause_at: 10 Start presses did not open the pause screen");
            } else {
                spdlog::info("[vr-diag] diag_pause_at={:.1f}: pressing Start (try {})", d.diag_pause_at,
                             sPauseDiagTries);
                GameInteractor_SetEmulatedButtons(BTN_START);
                sPauseDiagWait = 20;
            }
        }
    }
    return sPresentMode;
}

// The skybox scale in VR: far enough for stereo to read it as infinity. On the cinema screen the
// game's own projection is used, so the box must stay inside its far plane (zFar).
extern "C" f32 VrFrame_SkyboxScale(void) {
    if (VR_GetPresentation() == VR_PRESENT_IMMERSIVE || gPlayState == nullptr) {
        return 100.0f;
    }
    // The box's corners are about 126 * sqrt(3) = 218 units out at scale 1.
    const f32 zFar = gPlayState->view.zFar > 100.0f ? gPlayState->view.zFar : 12800.0f;
    f32 s = 0.85f * zFar / 218.0f;
    if (s > 100.0f) s = 100.0f;
    if (s < 1.0f) s = 1.0f;
    return s;
}

// [vr-sky]: one line whenever the sky's inputs change, to tell "the scene draws no sky" from
// "VR lost it" (the Deku Tree / Navi intro report).
extern "C" void VrFrame_LogSky(PlayState* play) {
    static std::string sLast;
    char line[256];
    snprintf(line, sizeof(line),
             "scene 0x%x layer %d skyboxId %d disabled %d unk_140 %d fogNear %d bgCover %d filterAlpha %.2f "
             "csIndex 0x%x cs %d presentation %d",
             play->sceneNum, gSaveContext.sceneLayer, play->skyboxId, play->envCtx.skyboxDisabled,
             play->skyboxCtx.unk_140, play->lightCtx.fogNear, play->bgCoverAlpha,
             (play->lightCtx.fogNear < 980 && play->skyboxId != SKYBOX_NONE)
                 ? (play->lightCtx.fogNear > 950 ? (1000 - play->lightCtx.fogNear) * 0.02f : 1.0f)
                 : 0.0f,
             gSaveContext.cutsceneIndex, play->csCtx.state, VR_GetPresentation());
    // fogNear moves every tick in some scenes: log it at 10-unit steps only.
    std::string key = line;
    const size_t f = key.find("fogNear ");
    if (f != std::string::npos) {
        const size_t e = key.find(' ', f + 8);
        key.replace(f + 8, e - (f + 8), std::to_string(play->lightCtx.fogNear / 10));
    }
    if (key != sLast) {
        sLast = key;
        spdlog::info("[vr-sky] {}", line);
    }
}

#endif // SOH_STEAM_FRAME && __ANDROID__
