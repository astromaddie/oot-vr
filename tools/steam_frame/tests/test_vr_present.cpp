/*
 * Host test of the Steam Frame presentation math (libultraship/src/fast/vr_present.cpp, the
 * production file): the roll-free head-locked HUD pose, the 3D cinema screen's projection views
 * (eye pose + asymmetric FOV from the screen corners), the stereo split of the game's projection,
 * the mode-change fade and the cinema decision.
 * Run: tools/steam_frame/tests/run_tests.sh
 */
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "fast/vr_present.h"

static int failures;
#define CHECK(cond, what)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__);   \
            failures++;                                                \
        }                                                              \
    } while (0)

static bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

static const float kPi = 3.14159265358979f;

static void quat_axis(float ax, float ay, float az, float deg, float q[4]) {
    const float h = deg * kPi / 360.0f;
    q[0] = ax * std::sin(h);
    q[1] = ay * std::sin(h);
    q[2] = az * std::sin(h);
    q[3] = std::cos(h);
}

static void quat_mul(const float a[4], const float b[4], float o[4]) {
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static void rotate(const float q[4], const float v[3], float o[3]) {
    const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
    const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
    const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

// yaw (about +Y), then pitch (about the new +X), then roll (about the new +Z)
static void head_quat(float yaw, float pitch, float roll, float q[4]) {
    float qy[4], qp[4], qr[4], t[4];
    quat_axis(0, 1, 0, yaw, qy);
    quat_axis(1, 0, 0, pitch, qp);
    quat_axis(0, 0, 1, roll, qr);
    quat_mul(qy, qp, t);
    quat_mul(t, qr, q);
}

static void test_roll_free() {
    const float right_in[3] = { 1, 0, 0 }, fwd_in[3] = { 0, 0, -1 };
    for (float roll : { -40.0f, 0.0f, 25.0f, 60.0f }) {
        float q[4], s[4], f0[3], f1[3], r1[3];
        head_quat(30.0f, 20.0f, roll, q);
        vr_present_strip_roll(q, s);
        rotate(q, fwd_in, f0);
        rotate(s, fwd_in, f1);
        rotate(s, right_in, r1);
        CHECK(near(f0[0], f1[0]) && near(f0[1], f1[1]) && near(f0[2], f1[2]), "roll strip keeps the gaze");
        CHECK(near(r1[1], 0.0f), "roll strip leaves the right vector level");
        float ref[4];
        head_quat(30.0f, 20.0f, 0.0f, ref);
        const float dot = s[0] * ref[0] + s[1] * ref[1] + s[2] * ref[2] + s[3] * ref[3];
        CHECK(near(std::fabs(dot), 1.0f), "roll strip = yaw * pitch of the same head");
    }
    // Straight down with a roll: still level, facing the way the head's top points.
    float q[4], s[4], r1[3];
    head_quat(-70.0f, -90.0f, 35.0f, q);
    vr_present_strip_roll(q, s);
    rotate(s, right_in, r1);
    CHECK(near(r1[1], 0.0f, 1e-3f), "straight down: no roll");

    // The HUD quad: the same pose with or without head roll.
    VrPresentPose head;
    head.pos[0] = 0.2f;
    head.pos[1] = 1.6f;
    head.pos[2] = -0.1f;
    head_quat(45.0f, -10.0f, 0.0f, head.quat);
    const VrPresentPose a = vr_present_head_locked(head, 0.1f, -0.2f, 2.0f);
    head_quat(45.0f, -10.0f, 30.0f, head.quat);
    const VrPresentPose b = vr_present_head_locked(head, 0.1f, -0.2f, 2.0f);
    for (int i = 0; i < 3; i++) CHECK(near(a.pos[i], b.pos[i]), "HUD position ignores roll");
    const float qdot = a.quat[0] * b.quat[0] + a.quat[1] * b.quat[1] + a.quat[2] * b.quat[2] + a.quat[3] * b.quat[3];
    CHECK(near(std::fabs(qdot), 1.0f), "HUD orientation ignores roll");
    // 2 m ahead along the (pitched) gaze, offsets in the head frame.
    float d[3] = { a.pos[0] - 0.2f, a.pos[1] - 1.6f, a.pos[2] + 0.1f };
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    CHECK(near(len, std::sqrt(4.0f + 0.01f + 0.04f)), "HUD distance and offsets");
}

static void test_cinema_views() {
    VrPresentPose head;
    head.pos[1] = 1.6f;
    head_quat(0.0f, -30.0f, 20.0f, head.quat); // looking down and rolled: the screen stays upright
    const VrCinemaScreen s = vr_cinema_place(head, 3.0f, 3.6f, 16.0f / 9.0f);
    CHECK(near(s.pose.pos[0], 0.0f) && near(s.pose.pos[1], 1.6f) && near(s.pose.pos[2], -3.0f), "screen 3 m ahead");
    CHECK(near(s.height, 2.025f), "16:9 screen height");
    CHECK(near(std::fabs(s.pose.quat[3]), 1.0f), "screen yaw-only, facing the head");

    const float eyes[2][3] = { { -0.032f, 1.6f, 0.0f }, { 0.032f, 1.6f, 0.0f } };
    for (int e = 0; e < 2; e++) {
        VrPresentPose p;
        VrPresentFov f;
        CHECK(vr_cinema_eye_view(s, eyes[e], &p, &f), "eye in front of the screen");
        const float ex = eyes[e][0];
        CHECK(near(f.left, std::atan2(-1.8f - ex, 3.0f)), "left edge");
        CHECK(near(f.right, std::atan2(1.8f - ex, 3.0f)), "right edge");
        CHECK(near(f.up, std::atan2(1.0125f, 3.0f)) && near(f.down, -std::atan2(1.0125f, 3.0f)), "top and bottom");
        CHECK(near(p.pos[0], ex) && near(p.pos[1], 1.6f) && near(p.pos[2], 0.0f), "pose = eye position");
        CHECK(near(p.quat[0], s.pose.quat[0]) && near(p.quat[1], s.pose.quat[1]) && near(p.quat[3], s.pose.quat[3]),
              "pose = screen orientation");
        // The FOV's corners land on the screen corners: eye + dir(tan) * dist = corner.
        CHECK(near(ex + std::tan(f.left) * 3.0f, -1.8f) && near(ex + std::tan(f.right) * 3.0f, 1.8f),
              "corners on the screen");
    }
    // A screen to the player's left (head yawed +90: looking down -X), eye moved off-axis.
    head_quat(90.0f, 0.0f, 0.0f, head.quat);
    head.pos[0] = 1.0f;
    head.pos[2] = 2.0f;
    const VrCinemaScreen s2 = vr_cinema_place(head, 3.0f, 3.6f, 16.0f / 9.0f);
    CHECK(near(s2.pose.pos[0], -2.0f) && near(s2.pose.pos[2], 2.0f), "screen follows the yaw");
    const float eye[3] = { 1.0f, 1.8f, 2.5f }; // the screen's +X axis is world -Z here
    VrPresentPose p;
    VrPresentFov f;
    CHECK(vr_cinema_eye_view(s2, eye, &p, &f), "rotated screen visible");
    // In the screen frame the eye is at x = -0.5 (world +Z is the screen's -X), y = 0.2, z = 3.
    CHECK(near(f.left, std::atan2(-1.8f + 0.5f, 3.0f)) && near(f.right, std::atan2(1.8f + 0.5f, 3.0f)),
          "rotated screen: horizontal");
    CHECK(near(f.up, std::atan2(1.0125f - 0.2f, 3.0f)) && near(f.down, std::atan2(-1.0125f - 0.2f, 3.0f)),
          "rotated screen: vertical");
    const float behind[3] = { -3.0f, 1.6f, 2.0f };
    CHECK(!vr_cinema_eye_view(s2, behind, &p, &f), "eye behind the screen");
}

// guPerspective-like (row-vector, w = -z) times a look-at at the origin looking down -Z.
static void perspective(float fovy_deg, float aspect, float n, float fa, float P[4][4]) {
    memset(P, 0, sizeof(float) * 16);
    const float f = 1.0f / std::tan(fovy_deg * kPi / 360.0f);
    P[0][0] = f / aspect;
    P[1][1] = f;
    P[2][2] = (n + fa) / (n - fa);
    P[2][3] = -1.0f;
    P[3][2] = 2.0f * n * fa / (n - fa);
}

static void project(const float M[4][4], float x, float y, float z, float* ndc_x, float* w_out) {
    const float v[4] = { x, y, z, 1.0f };
    float c[4] = {};
    for (int col = 0; col < 4; col++)
        for (int r = 0; r < 4; r++) c[col] += v[r] * M[r][col];
    *ndc_x = c[0] / c[3];
    *w_out = c[3];
}

static void test_stereo_split() {
    const float disp = vr_cinema_ndc_disparity(0.063f, 0.55f, 3.6f);
    CHECK(near(disp * 3.6f / 2.0f, 0.55f * 0.063f), "disparity at infinity = depth x IPD on the screen");
    CHECK(vr_cinema_ndc_disparity(0.063f, 0.0f, 3.6f) == 0.0f, "depth 0 = flat");

    float P[4][4], L[4][4], R[4][4];
    perspective(60.0f, 4.0f / 3.0f, 10.0f, 12800.0f, P);
    const float C = 150.0f;
    vr_cinema_eye_matrix(P, 0, disp, C, 0.75f, L);
    vr_cinema_eye_matrix(P, 1, disp, C, 0.75f, R);
    float m, l, r, w;
    // On the convergence plane: both eyes agree with the (aspect-corrected) mono image.
    project(P, 30.0f, 5.0f, -C, &m, &w);
    project(L, 30.0f, 5.0f, -C, &l, &w);
    project(R, 30.0f, 5.0f, -C, &r, &w);
    CHECK(near(l, m * 0.75f) && near(r, m * 0.75f), "zero parallax at the convergence distance");
    // Far away: right minus left tends to the full disparity (uncrossed: behind the screen).
    project(L, 0.0f, 0.0f, -1.0e6f, &l, &w);
    project(R, 0.0f, 0.0f, -1.0e6f, &r, &w);
    CHECK(near(r - l, disp * (1.0f - C / 1.0e6f), 1e-5f), "full disparity at infinity");
    CHECK(r > l, "far objects uncrossed (left image to the left)");
    // Nearer than the convergence plane: crossed.
    project(L, 0.0f, 0.0f, -50.0f, &l, &w);
    project(R, 0.0f, 0.0f, -50.0f, &r, &w);
    CHECK(near(r - l, disp * (1.0f - C / 50.0f), 1e-5f) && r < l, "near objects crossed");
    // Orthographic projections (2D overlays) stay on the screen plane.
    float O[4][4] = {}, OL[4][4], OR[4][4];
    O[0][0] = 2.0f / 320.0f;
    O[1][1] = 2.0f / 240.0f;
    O[2][2] = -0.001f;
    O[3][3] = 1.0f;
    vr_cinema_eye_matrix(O, 0, disp, C, 1.0f, OL);
    vr_cinema_eye_matrix(O, 1, disp, C, 1.0f, OR);
    CHECK(memcmp(OL, O, sizeof(O)) == 0 && memcmp(OR, O, sizeof(O)) == 0, "ortho untouched");
}

static void test_fade() {
    VrPresentFade f;
    CHECK(vr_present_fade_black(&f, 5.0) == 0.0f, "no fade by default");
    vr_present_fade_trigger(&f, 10.0);
    CHECK(vr_present_fade_black(&f, 10.0) == 1.0f, "black at once");
    CHECK(vr_present_fade_black(&f, 10.24) == 1.0f, "held black 0.25 s");
    CHECK(near(vr_present_fade_black(&f, 10.25 + 0.175), 0.5f, 1e-3f), "half way through the 0.35 s fade-in");
    CHECK(vr_present_fade_black(&f, 10.61) == 0.0f && f.start_s < 0.0, "over after 0.6 s");
}

static void test_decide() {
    char why[512];
    VrPresentInputs in;
    CHECK(vr_present_decide(in, why, sizeof(why)) == VR_PRESENT_IMMERSIVE, "gameplay immersive");
    CHECK(strstr(why, "csCtx=0") && strstr(why, "setting=3D CINEMA"), "why names every condition");
    in.cs_active = true;
    CHECK(vr_present_decide(in, why, sizeof(why)) == VR_PRESENT_CINEMA, "scripted cutscene -> cinema");
    in.cutscene_setting_cinema = false;
    CHECK(vr_present_decide(in, why, sizeof(why)) == VR_PRESENT_IMMERSIVE && strstr(why, "IMMERSIVE"),
          "IMMERSIVE setting keeps 6DoF");
    in = VrPresentInputs();
    in.player_cs_mode = true;
    CHECK(vr_present_decide(in, why, sizeof(why)) == VR_PRESENT_IMMERSIVE, "talk with the gameplay camera stays");
    in.cam_cutscene = true;
    CHECK(vr_present_decide(in, why, sizeof(why)) == VR_PRESENT_CINEMA, "Link held + cutscene camera -> cinema");
    in = VrPresentInputs();
    in.intro_cs = true;
    CHECK(vr_present_decide(in, why, sizeof(why)) == VR_PRESENT_CINEMA, "entrance cutscene -> cinema");
    in = VrPresentInputs();
    in.prerender_room = true;
    in.cs_active = true;
    CHECK(vr_present_decide(in, why, sizeof(why)) == VR_PRESENT_CINEMA_FLAT, "pre-rendered room -> flat cinema");
    in.prerender_setting_cinema = false;
    CHECK(vr_present_decide(in, why, sizeof(why)) == VR_PRESENT_CINEMA, "room setting off, cutscene still cinema");
    CHECK(vr_present_decide(in, nullptr, 0) == VR_PRESENT_CINEMA, "why may be null");
}

// P = M * Persp with M a look-at (rotation about Y by 30 degrees and a translation), row-vector.
static void look_at_times(const float Pp[4][4], float out[4][4]) {
    const float c = std::cos(0.5236f), sn = std::sin(0.5236f);
    float M[4][4] = { { c, 0, -sn, 0 }, { 0, 1, 0, 0 }, { sn, 0, c, 0 }, { 120.0f, -40.0f, 300.0f, 1 } };
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++) {
            float acc = 0.0f;
            for (int j = 0; j < 4; j++) acc += M[r][j] * Pp[j][k];
            out[r][k] = acc;
        }
}

static void test_window_stereo() {
    float P[4][4], PM[4][4];
    perspective(60.0f, 4.0f / 3.0f, 10.0f, 12800.0f, P);
    CHECK(near(vr_cinema_near_w(P), 10.0f, 1e-2f), "near plane of the game projection");
    look_at_times(P, PM);
    CHECK(near(vr_cinema_near_w(PM), 10.0f, 1e-2f), "near plane survives the look-at");
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) PM[r][c] *= 2.0f; // N64 perspective scale: w units scale with it
    CHECK(near(vr_cinema_near_w(PM), 20.0f, 2e-2f), "near plane in w units with a perspective scale");
    float O[4][4] = {};
    O[0][0] = O[1][1] = 0.01f;
    O[2][2] = -0.001f;
    O[3][3] = 1.0f;
    CHECK(vr_cinema_near_w(O) == 0.0f, "ortho has no near plane");

    // Converged at the nearest visible depth: nothing in the scene is in front of the screen plane.
    const float disp = vr_cinema_ndc_disparity(0.063f, 0.55f, 3.6f);
    const float nearest = 80.0f;
    float L[4][4], R[4][4];
    vr_cinema_eye_matrix(P, 0, disp, nearest, 1.0f, L);
    vr_cinema_eye_matrix(P, 1, disp, nearest, 1.0f, R);
    bool uncrossed = true;
    for (float z : { -80.0f, -81.0f, -150.0f, -1000.0f, -12000.0f }) {
        float l, r, w;
        project(L, 3.0f, 0.0f, z, &l, &w);
        project(R, 3.0f, 0.0f, z, &r, &w);
        uncrossed &= (r - l) >= -1e-6f;
    }
    CHECK(uncrossed, "window stereo: every depth beyond the convergence is uncrossed");

    float c = vr_cinema_convergence_step(0.0f, false, 80.0f, 10.0f);
    CHECK(near(c, 80.0f), "first frame takes the nearest depth");
    float c1 = vr_cinema_convergence_step(c, true, 40.0f, 10.0f);
    CHECK(c1 < 61.0f && c1 >= 40.0f, "nearer content: fast");
    float c2 = vr_cinema_convergence_step(40.0f, true, 400.0f, 10.0f);
    CHECK(c2 > 40.0f && c2 < 60.0f, "farther content: slow");
    CHECK(vr_cinema_convergence_step(40.0f, true, 2.0f, 10.0f) >= 10.0f, "never nearer than the near plane");
    CHECK(near(vr_cinema_convergence_step(55.0f, true, 0.0f, 10.0f), 55.0f), "no vertex seen: hold");
}

static void test_eye_size() {
    CHECK(vr_eye_size(1728, 4096, 1.0f, 2160) == 2160, "1728 recommended: up to 2160");
    CHECK(vr_eye_size(2160, 4096, 1.0f, 2160) == 2160, "2160 recommended (SteamVR set to 2160): kept");
    CHECK(vr_eye_size(2400, 4096, 1.0f, 2160) == 2400, "more than the target recommended: never scaled down");
    CHECK(vr_eye_size(1728, 4096, 0.75f, 2160) == 1620, "user scale on top of the target");
    CHECK(vr_eye_size(2160, 2500, 1.5f, 2160) == 2500, "clamped to the runtime maximum");
    CHECK(vr_eye_size(1728, 4096, 9.0f, 2160) == 3240, "user scale clamped to 1.5");
    CHECK(vr_eye_size(0, 4096, 1.0f, 2160) == 0, "no recommendation");
}

static void test_refresh() {
    const float frame[] = { 72.0f, 90.0f, 120.0f, 144.0f };
    CHECK(vr_refresh_pick(frame, 4, 120.0f) == 120.0f, "120 asked, 120 offered");
    CHECK(vr_refresh_pick(frame, 4, 100.0f) == 90.0f, "highest offered at or below the setting");
    CHECK(vr_refresh_pick(frame, 4, 60.0f) == 72.0f, "below everything: the lowest offered");
    CHECK(vr_refresh_pick(frame, 0, 120.0f) == 0.0f, "nothing offered");
    CHECK(vr_refresh_fallback(frame, 4, 120.0f) == 90.0f, "120 falls back to 90");
    CHECK(vr_refresh_fallback(frame, 4, 90.0f) == 72.0f, "90 falls back to 72");
    CHECK(vr_refresh_fallback(frame, 4, 72.0f) == 0.0f, "72 is the floor");
    const float odd[] = { 80.0f, 120.0f };
    CHECK(vr_refresh_fallback(odd, 2, 120.0f) == 80.0f, "no 90 or 72: the highest below");

    VrRefreshGovernor g;
    double t = 0.0;
    bool fell = false;
    // 120 Hz asked, 100 fps delivered (below 108): two 10 s windows, then the fallback.
    for (int i = 0; i < 100 * 25 && !fell; i++, t += 0.01) {
        fell = vr_refresh_governor_frame(&g, t, 120.0f, true);
    }
    CHECK(fell && t > 19.9 && t < 20.2, "fallback after two slow 10 s windows");
    VrRefreshGovernor ok;
    fell = false;
    t = 0.0;
    for (int i = 0; i < 120 * 40; i++, t += 1.0 / 119.0) {
        fell |= vr_refresh_governor_frame(&ok, t, 120.0f, true);
    }
    CHECK(!fell, "no fallback at full rate");
    VrRefreshGovernor off;
    fell = false;
    t = 0.0;
    for (int i = 0; i < 100 * 40; i++, t += 0.01) {
        fell |= vr_refresh_governor_frame(&off, t, 120.0f, (i / 900) % 2 == 0); // doffed every 9 s
    }
    CHECK(!fell, "windows count worn time only");
}

int main() {
    test_window_stereo();
    test_refresh();
    test_eye_size();
    test_roll_free();
    test_cinema_views();
    test_stereo_split();
    test_fade();
    test_decide();
    if (failures) {
        fprintf(stderr, "test_vr_present: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_vr_present: ok\n");
    return 0;
}
