#pragma once
#include <libultraship/bridge/consolevariablebridge.h>

static const char* sVrTpBindings[2][6] = {
    { "gVrBindTpLTrigger", "gVrBindTpLGrip", "gVrBindTpLPrimary", "gVrBindTpLSecondary", "gVrBindTpLStickClick", "gVrBindTpLMenu" },
    { "gVrBindTpRTrigger", "gVrBindTpRGrip", "gVrBindTpRPrimary", "gVrBindTpRSecondary", "gVrBindTpRStickClick", "gVrBindTpRMenu" }
};
static inline void VrFrame_InitTpBindings(void) {
    if (CVarGetInteger("gVrTpBindingsVersion", 0) >= 1) return;
    static const char* old[2][6] = {
        { "gVrBindLTrigger", "gVrBindLGrip", "gVrBindLPrimary", "gVrBindLSecondary", "gVrBindLStickClick", "gVrBindLMenu" },
        { "gVrBindRTrigger", "gVrBindRGrip", "gVrBindRPrimary", "gVrBindRSecondary", "gVrBindRStickClick", "gVrBindRMenu" }
    };
    static const int32_t defaults[2][6] = {
        { BTN_Z, BTN_R, BTN_CLEFT, BTN_CRIGHT, 0, 0 },
        { BTN_B, 0, BTN_A, BTN_CDOWN, 0, BTN_START }
    };
    for (int hand = 0; hand < 2; ++hand) {
        for (int button = 0; button < 6; ++button) {
            int32_t value = CVarGetInteger(old[hand][button], defaults[hand][button]);
            if (hand == 0 && button == 1 && value == BTN_R) value = 0;
            if (hand == 1 && button == 3 && value == BTN_CDOWN) value = BTN_B;
            CVarRegisterInteger(sVrTpBindings[hand][button], value);
        }
    }
    CVarSetInteger("gVrTpBindingsVersion", 1);
    CVarSave();
}
