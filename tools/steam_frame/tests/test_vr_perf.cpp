/*
 * The [vr-perf] line the Frame build logs every 10 s comes from sfvr_perf. This checks it with the
 * stage names libultraship/src/fast/vr_frame.cpp passes (tick, eyes, hud, other): fps, missed,
 * cpu and each stage, eye and gpu fields, the way the standard (section 8) and the other ports
 * write them. The device-only part (the timer query, xrWaitFrame) is not covered here.
 */
#include <cstdio>
#include <cstring>

#include "sfvr/sfvr_perf.h"

int main() {
    static const char* const names[] = { "tick", "eyes", "hud", "other" };
    sfvr_perf p;
    sfvr_perf_init(&p, 10.0, names, 4);
    char line[256];
    int emitted = 0;
    // 72 Hz for 10.5 s of synthetic time; every 4th frame misses; the GPU time is unknown (-1).
    for (int i = 0; i < 756; i++) {
        const double t = i / 72.0;
        const float stages[4] = { i % 4 == 0 ? 2.0f : 0.0f, i % 2 == 0 ? 3.0f : 0.0f, 0.5f, 1.0f };
        const float eye[2] = { stages[1], stages[1] };
        sfvr_perf_add_frame(&p, t, i % 4 == 0, stages, eye, -1.0f);
        if (sfvr_perf_poll(&p, t, line, sizeof(line))) {
            emitted++;
            printf("%s\n", line);
            if (strncmp(line, "[vr-perf] fps=", 14) != 0 || !strstr(line, " missed=") || !strstr(line, " cpu=") ||
                !strstr(line, " tick=") || !strstr(line, " eyes=") || !strstr(line, " hud=") ||
                !strstr(line, " other=") || !strstr(line, " eye=") || !strstr(line, " gpu=n/a")) {
                fprintf(stderr, "FAIL: a field is missing from the line\n");
                return 1;
            }
        }
    }
    if (emitted != 1) {
        fprintf(stderr, "FAIL: expected one line in 10.5 s, got %d\n", emitted);
        return 1;
    }
    printf("vr_perf: all checks passed\n");
    return 0;
}
