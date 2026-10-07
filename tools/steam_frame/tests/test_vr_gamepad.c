// Production migration exercised against an in-memory CVar store.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <libultraship/libultra/controller.h>
#include "soh/Enhancements/steam-frame/VrGamepad.h"
static struct { char key[64]; int32_t value; } values[64];
static int count, saves;
int32_t CVarGetInteger(const char* key, int32_t fallback) {
    for (int i = 0; i < count; ++i) if (!strcmp(key, values[i].key)) return values[i].value;
    return fallback;
}
void CVarSetInteger(const char* key, int32_t value) {
    for (int i = 0; i < count; ++i) if (!strcmp(key, values[i].key)) { values[i].value = value; return; }
    strcpy(values[count].key, key); values[count++].value = value;
}
void CVarRegisterInteger(const char* key, int32_t value) {
    for (int i = 0; i < count; ++i) if (!strcmp(key, values[i].key)) return;
    CVarSetInteger(key, value);
}
void CVarSave(void) { ++saves; }
int main(void) {
    CVarSetInteger("gVrBindLGrip", BTN_R);
    CVarSetInteger("gVrBindRSecondary", BTN_CDOWN);
    CVarSetInteger("gVrBindLTrigger", BTN_Z | BTN_L); // custom combination
    CVarSetInteger("gVrBindSelLGrip", BTN_Z);
    CVarSetInteger("gVrBindOcaRTrigger", BTN_A);
    VrFrame_InitTpBindings();
    assert(CVarGetInteger("gVrBindTpLGrip", -1) == 0);
    assert(CVarGetInteger("gVrBindTpRSecondary", -1) == BTN_B);
    assert(CVarGetInteger("gVrBindTpLTrigger", -1) == (BTN_Z | BTN_L));
    assert(CVarGetInteger("gVrBindTpRMenu", -1) == BTN_START);
    assert(CVarGetInteger("gVrBindLGrip", -1) == BTN_R);
    assert(CVarGetInteger("gVrBindRSecondary", -1) == BTN_CDOWN);
    assert(CVarGetInteger("gVrBindSelLGrip", -1) == BTN_Z);
    assert(CVarGetInteger("gVrBindOcaRTrigger", -1) == BTN_A);
    CVarSetInteger("gVrBindTpRSecondary", BTN_CDOWN); // explicit new TP custom choice wins
    VrFrame_InitTpBindings();
    assert(CVarGetInteger("gVrBindTpRSecondary", -1) == BTN_CDOWN && saves == 1);
    count = saves = 0;
    CVarSetInteger("gVrBindLGrip", BTN_R | BTN_Z);
    CVarSetInteger("gVrBindRSecondary", 0);
    CVarSetInteger("gVrBindTpLPrimary", BTN_L); // existing target key wins too
    VrFrame_InitTpBindings();
    assert(CVarGetInteger("gVrBindTpLGrip", -1) == (BTN_R | BTN_Z));
    assert(CVarGetInteger("gVrBindTpRSecondary", -1) == 0);
    assert(CVarGetInteger("gVrBindTpLPrimary", -1) == BTN_L);
    puts("vr_gamepad: defaults, custom combinations, existing target keys, FP/ocarina preservation, idempotence passed");
    return 0;
}
