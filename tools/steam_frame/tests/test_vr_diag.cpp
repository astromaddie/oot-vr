/*
 * Host test of the Steam Frame diagnostics that need no device (libultraship/src/fast/vr_frame_diag.cpp,
 * the production file): the vr-diag.ini parser, the force_render synthetic head and the BMP writer
 * the eye dumps use. Run: tools/steam_frame/tests/run_tests.sh
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "fast/vr_frame_diag.h"

static int failures;
#define CHECK(cond, what)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__);   \
            failures++;                                                \
        }                                                              \
    } while (0)

static std::vector<std::string> s_unknown;
static void note_unknown(const char* k) { s_unknown.push_back(k); }

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

int main() {
    // ---- parser
    VrFrameDiag d;
    vr_frame_diag_parse("# a comment\n[diag]\nforce_render = 1\r\ndiag_yaw=90\n  dump_frame = 240  \n"
                        "timing=on\nautostart = yes\nautostart_entrance = 0x1d1\nstereo_divisor = 1\nbogus_key = 3\nnot a pair\n",
                        &d, note_unknown);
    CHECK(d.force_render, "force_render read");
    CHECK(near(d.diag_yaw, 90.0f), "diag_yaw read");
    CHECK(d.dump_frame == 240, "dump_frame read, spaces trimmed");
    CHECK(d.timing, "timing=on read");
    CHECK(d.autostart, "autostart=yes read");
    CHECK(d.autostart_entrance == 0x1d1, "autostart_entrance read as hex");
    CHECK(d.stereo_divisor == 1, "stereo_divisor read");
    CHECK(s_unknown.size() == 1 && s_unknown[0] == "bogus_key", "an unknown key is reported once");

    VrFrameDiag off;
    vr_frame_diag_parse("force_render=0\ntiming=0\n", &off, nullptr);
    CHECK(!off.force_render && !off.timing && off.dump_frame == 0 && !off.autostart, "defaults and explicit off");
    CHECK(off.autostart_entrance == 0xEE, "autostart_entrance defaults to Kokiri Forest");
    CHECK(off.stereo_divisor == 0, "stereo_divisor defaults to 'leave the CVar alone'");
    VrFrameDiag wild;
    vr_frame_diag_parse("stereo_divisor = 9\n", &wild, nullptr);
    CHECK(wild.stereo_divisor == 4, "stereo_divisor is clamped to 4");
    vr_frame_diag_parse("stereo_divisor = -3\n", &wild, nullptr);
    CHECK(wild.stereo_divisor == 0, "a negative stereo_divisor means off");

    // ---- synthetic head
    VrFrameDiag h;
    float hp[3], hq[4];
    vr_frame_synth_head(h, hp, hq);
    CHECK(near(hp[0], 0) && near(hp[1], 1.6f) && near(hp[2], 0), "synthetic head is 1.6 m up");
    CHECK(near(hq[3], 1.0f) && near(hq[1], 0.0f), "yaw 0 is the identity");

    float e0[3], e1[3], q[4];
    vr_frame_synth_eye(h, 0, e0, q);
    vr_frame_synth_eye(h, 1, e1, q);
    float dx = e1[0] - e0[0], dy = e1[1] - e0[1], dz = e1[2] - e0[2];
    CHECK(near(std::sqrt(dx * dx + dy * dy + dz * dz), 0.064f), "eyes are 64 mm apart (+-32 mm)");
    CHECK(e0[0] < e1[0], "the left eye is on the left (-X)");
    CHECK(near((e0[0] + e1[0]) * 0.5f, 0.0f) && near(e0[1], 1.6f), "eyes centred on the head");

    VrFrameDiag y;
    y.diag_yaw = 90.0f;
    vr_frame_synth_head(y, hp, hq);
    CHECK(near(hq[1], std::sin(3.14159265f / 4), 1e-4f) && near(hq[3], std::cos(3.14159265f / 4), 1e-4f),
          "yaw 90 is a quarter turn about +Y");
    vr_frame_synth_eye(y, 0, e0, q);
    vr_frame_synth_eye(y, 1, e1, q);
    // Turned left by 90 degrees, the head's right vector points along -Z... its +X maps to (0,0,-1).
    CHECK(near(e1[0] - e0[0], 0.0f) && near(e1[2] - e0[2], -0.064f), "at yaw 90 the eyes are split along Z");

    // ---- BMP writer
    const int w = 5, hgt = 3; // 5*3 = 15 bytes per row, padded to 16
    std::vector<uint8_t> px(w * hgt * 4);
    for (int i = 0; i < w * hgt; i++) {
        px[i * 4 + 0] = 10;   // R
        px[i * 4 + 1] = 20;   // G
        px[i * 4 + 2] = 30;   // B
        px[i * 4 + 3] = i % 2 ? 255 : 0;
    }
    const char* path = "/tmp/vr_diag_test.bmp";
    unsigned pct = 0;
    CHECK(vr_frame_write_bmp24(path, px.data(), w, hgt, &pct), "bmp written");
    CHECK(pct == 46, "opaque percentage (7 of 15 pixels are opaque)");
    FILE* f = fopen(path, "rb");
    CHECK(f != nullptr, "bmp readable");
    if (f) {
        uint8_t buf[256];
        const size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        CHECK(n == 54 + 16 * 3, "file size is header + padded rows");
        CHECK(buf[0] == 'B' && buf[1] == 'M', "BMP magic");
        int32_t bw, bh;
        memcpy(&bw, buf + 18, 4);
        memcpy(&bh, buf + 22, 4);
        CHECK(bw == w && bh == hgt, "BMP size fields");
        CHECK(buf[54] == 30 && buf[55] == 20 && buf[56] == 10, "pixels are stored BGR");
    }
    remove(path);
    CHECK(!vr_frame_write_bmp24("/nonexistent-dir/x.bmp", px.data(), w, hgt, nullptr), "an unwritable path fails cleanly");

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("vr_frame_diag: all checks passed\n");
    return 0;
}
