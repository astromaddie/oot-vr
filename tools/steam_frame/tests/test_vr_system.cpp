/*
 * Host test of the Steam Frame system layer (libultraship/src/fast/vr_system.cpp, the production
 * file) on top of sfvr's real View state machine, with a fake clock fed in as `now_s`.
 * Run: tools/steam_frame/tests/run_tests.sh
 *
 * Covers standard.md section 1 as the Frame build implements it: View short press = Back (only),
 * hold 1 s = recentre once, hold 3 s = recentre then standing height, one event per press; Menu +
 * View held 0.5 s = settings menu, once per chord, and the View press that made it is spent; R Menu
 * is held back from the game while View is down; focus loss cancels a press; a button already down
 * when the session starts is ignored.
 */
#include <cstdio>

#include "fast/vr_system.h"

static int failures;
#define CHECK(cond, what)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__);   \
            failures++;                                                \
        }                                                              \
    } while (0)

struct Counts {
    int back = 0, recentre = 0, recalibrate = 0, toggle = 0, held_menu_frames = 0;
};

static double g_now = 100.0;

// Drive the layer for `seconds` at 72 Hz with the given button levels; returns what happened.
static Counts run(double seconds, bool view, bool menu, bool focused = true) {
    Counts c;
    const double dt = 1.0 / 72.0;
    for (double t = 0.0; t < seconds; t += dt) {
        VrSysIn in;
        in.view = view;
        in.menu = menu;
        in.focused = focused;
        in.now_s = g_now;
        const VrSysOut o = vr_system_step(in);
        c.back += o.back;
        c.recentre += o.recentre;
        c.recalibrate += o.recalibrate;
        c.toggle += o.toggle_settings;
        c.held_menu_frames += o.hold_menu_from_game;
        g_now += dt;
    }
    return c;
}

static Counts press(double seconds, bool menu = false) {
    Counts a = run(seconds, true, menu);
    Counts b = run(0.1, false, false); // release, then a moment of nothing
    a.back += b.back;
    a.recentre += b.recentre;
    a.recalibrate += b.recalibrate;
    a.toggle += b.toggle;
    return a;
}

int main() {
    // A button already down when the session starts is not a press we saw begin.
    vr_system_reset();
    Counts c = run(0.3, true, false);
    c = press(0.0);
    CHECK(c.back == 0 && c.recentre == 0, "a View already down at the start is ignored");
    run(0.3, false, false);

    // Short press: Back, nothing else, and the pulse is about 150 ms.
    c = press(0.12);
    CHECK(c.back == 1 && c.recentre == 0 && c.recalibrate == 0 && c.toggle == 0, "short press is Back and only Back");
    CHECK(!vr_system_back_pulse(g_now + 0.2), "the Back pulse is over after 200 ms");
    {
        // pulse is live right after the press
        run(0.0, false, false);
        vr_system_reset();
        VrSysIn in;
        in.focused = true;
        in.view = false;
        in.now_s = g_now - 0.02;
        vr_system_step(in); // seen released: the layer is ready for a press
        in.view = true;
        in.now_s = g_now;
        vr_system_step(in); // press starts
        in.view = true;
        in.now_s = g_now + 0.05;
        vr_system_step(in);
        in.view = false;
        in.now_s = g_now + 0.10;
        const VrSysOut o = vr_system_step(in);
        CHECK(o.back, "release after 100 ms is a short press");
        CHECK(vr_system_back_pulse(in.now_s + 0.05), "the Back pulse is live for the next 150 ms");
        CHECK(!vr_system_back_pulse(in.now_s + 0.16), "and gone after that");
        g_now += 1.0;
    }

    // Hold 1.2 s: recentre once, no Back.
    run(0.3, false, false);
    c = press(1.2);
    CHECK(c.recentre == 1 && c.recalibrate == 0, "hold 1 s recentres once");
    CHECK(c.back == 0, "a held View is not also a Back");

    // Hold 3.2 s: recentre, then standing height; one of each.
    c = press(3.2);
    CHECK(c.recentre == 1 && c.recalibrate == 1, "hold 3 s ends with recentre + standing height, once each");

    // R Menu is held back from the game while View is down.
    run(0.3, false, false);
    c = run(0.2, true, false);
    CHECK(c.held_menu_frames > 0, "R Menu does not reach the game while View is down");
    run(0.2, false, false);
    c = run(0.2, false, true);
    CHECK(c.held_menu_frames == 0, "R Menu alone reaches the game");
    run(0.3, false, false);

    // Chord: View first, then Menu; 0.5 s after both are down the settings menu toggles, once.
    run(0.3, false, false);
    run(0.2, true, false);
    c = run(0.45, true, true);
    CHECK(c.toggle == 0, "the chord does not fire before 0.5 s");
    c = run(0.2, true, true);
    CHECK(c.toggle == 1, "the chord fires at 0.5 s");
    c = run(2.0, true, true);
    CHECK(c.toggle == 0 && c.recentre == 0 && c.recalibrate == 0, "holding on does not repeat it, recentre or recalibrate");
    c = run(0.2, false, false);
    CHECK(c.back == 0, "releasing after the chord is not a Back");
    run(0.3, false, false);

    // Chord the other way round (Menu first): the same, and the View press is still spent.
    run(0.2, false, true);
    c = run(0.6, true, true);
    CHECK(c.toggle == 1, "Menu then View also toggles the menu");
    c = run(1.5, true, false);
    CHECK(c.recentre == 0, "the View press that was part of the chord does not recentre later");
    c = run(0.2, false, false);
    CHECK(c.back == 0, "...and does not go Back");

    // A second chord needs a fresh press.
    run(0.3, false, false);
    c = run(0.7, true, true);
    CHECK(c.toggle == 1, "a second chord toggles again");
    run(0.3, false, false);

    // Focus lost mid-press: cancelled, no events, the release is not a Back.
    c = run(0.6, true, false);
    c = run(1.5, true, false, /*focused=*/false);
    CHECK(c.recentre == 0 && c.back == 0, "no events while the session is not focused");
    c = run(0.2, false, false, false);
    c = run(0.2, false, false, true);
    CHECK(c.back == 0 && c.recentre == 0, "focus loss cancelled the press");

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("vr_system: all checks passed\n");
    return 0;
}
