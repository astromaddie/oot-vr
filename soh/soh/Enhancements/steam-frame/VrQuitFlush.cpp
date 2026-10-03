// Steam Frame only: when SteamVR asks the app to end, the XR layer saves the settings and then calls
// this to let any save file write that is still running on the game's thread pool finish, so the
// process exit that follows cannot cut it in half (the XR layer waits for it for 2.5 s at most).
#if defined(SOH_STEAM_FRAME) && defined(__ANDROID__)

#include <fast/vr_frame.h>
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"

static void VrQuitFlush() {
    if (SaveManager::Instance != nullptr) {
        SaveManager::Instance->ThreadPoolWait();
    }
}

static void RegisterVrQuitFlush() {
    vr_frame_set_quit_flush(VrQuitFlush);
}

static RegisterShipInitFunc sRegisterVrQuitFlush(RegisterVrQuitFlush);

#endif // SOH_STEAM_FRAME && __ANDROID__
