#!/usr/bin/env python3
"""Binding table sanity for the Steam Frame build (standard.md section 10, lessons 33 and 35).

Reads libultraship/src/fast/vr_openxr.cpp (setup_input) and checks, without a runtime:

  * every input/output path suggested for an interaction profile is a real component of that
    profile. One bad path and SteamVR refuses the whole suggest call (the Index block of the
    GoldenEye Quest port carried trackpad/click, which Index does not have, and Index users had no
    bindings at all);
  * the Steam Frame profile is suggested first, and binds everything the game needs, with the
    physical layout of the Frame (A, B, X, Y and Menu on the RIGHT controller, View and the D-pad on
    the LEFT, a bumper on each);
  * action names are unique and valid path components, and localized names are unique (compared case
    insensitively): a duplicate localized name makes xrCreateAction fail with
    XR_ERROR_LOCALIZED_NAME_DUPLICATED, no actions are attached and the game takes no input at all
    (Halo CE VR, lesson 35);
  * every created action is bound by at least one profile (a dead action is a missing control).

The component lists are the OpenXR spec's and Valve's, as the other Frame ports bind them
(goldeneye-vr, HaloCE VR, F-Zero X VR).

Run: tools/steam_frame/tests/check_bindings.py [vr_openxr.cpp]    (exit 1 on any failure)
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[3] / "libultraship/src/fast/vr_openxr.cpp"

POSES = {"grip/pose", "aim/pose"}
HAPTIC = {"output/haptic"}
STICK = {"thumbstick", "thumbstick/x", "thumbstick/y", "thumbstick/click", "thumbstick/touch"}
PAD = {"trackpad", "trackpad/x", "trackpad/y", "trackpad/touch"}
BOTH = {"left", "right"}

PROFILES = {
    "/interaction_profiles/khr/simple_controller": (
        BOTH, {"select/click", "menu/click"} | POSES | HAPTIC),
    "/interaction_profiles/oculus/touch_controller": (
        BOTH, {"x/click", "x/touch", "y/click", "y/touch", "menu/click", "a/click", "a/touch", "b/click", "b/touch",
               "squeeze/value", "trigger/value", "trigger/touch", "thumbrest/touch"} | STICK | POSES | HAPTIC),
    "/interaction_profiles/valve/index_controller": (
        BOTH, {"system/click", "system/touch", "a/click", "a/touch", "b/click", "b/touch", "squeeze/value",
               "squeeze/force", "trigger/click", "trigger/value", "trigger/touch", "trackpad/force"}
        | STICK | PAD | POSES | HAPTIC),
    "/interaction_profiles/htc/vive_controller": (
        BOTH, {"system/click", "squeeze/click", "menu/click", "trigger/click", "trigger/value", "trackpad/click"}
        | PAD | POSES | HAPTIC),
    "/interaction_profiles/microsoft/motion_controller": (
        BOTH, {"menu/click", "squeeze/click", "trigger/value", "thumbstick/click", "trackpad/click"}
        | STICK | PAD | POSES | HAPTIC),
    "/interaction_profiles/valve/frame_controller_valve": (
        BOTH, {"a/click", "b/click", "x/click", "y/click", "menu/click", "view/click", "bumper/click",
               "trigger/value", "trigger/click", "squeeze/value", "dpad_up/click", "dpad_down/click",
               "dpad_left/click", "dpad_right/click"} | STICK | POSES | HAPTIC),
}
FRAME = "/interaction_profiles/valve/frame_controller_valve"

# The Frame is a split gamepad: this is the layout the game's mapping relies on.
FRAME_HAND = {"a/click": "right", "b/click": "right", "x/click": "right", "y/click": "right", "menu/click": "right",
              "view/click": "left", "dpad_up/click": "left", "dpad_down/click": "left", "dpad_left/click": "left",
              "dpad_right/click": "left"}
# Both hands carry these.
FRAME_BOTH = {"bumper/click", "trigger/value", "squeeze/value", "thumbstick", "thumbstick/click", "grip/pose",
              "aim/pose", "output/haptic"}
# Needed by the game: these actions must be bound on the Frame profile.
FRAME_NEEDS = ["grip_pose_action", "aim_pose_action", "trigger_action", "squeeze_action", "thumbstick_action",
               "thumbstick_click_action", "primary_action", "secondary_action", "menu_action", "haptic_action",
               "frame_x_action", "frame_y_action", "frame_view_action", "frame_bumper_action",
               "frame_dpad_action[0]", "frame_dpad_action[1]", "frame_dpad_action[2]", "frame_dpad_action[3]"]


def blocks(text):
    """(profile, body, offset) for each suggest("<profile>", { ... }); block, in file order."""
    for m in re.finditer(r'suggest\(\s*"([^"]+)",\s*\{(.*?)\}\s*\);', text, re.S):
        yield m.group(1), m.group(2), m.start()


def actions_created(text):
    """(name, localized) for every make_action / make_plain call."""
    pat = r'make_(?:action|plain)\(\s*"([^"]+)"\s*,\s*"([^"]+)"'
    return re.findall(pat, text)


def main():
    text = (Path(sys.argv[1]) if len(sys.argv) > 1 else SRC).read_text(encoding="utf-8-sig")
    bad = 0

    # --- action names
    created = actions_created(text)
    names = [n for n, _ in created]
    locs = [l.casefold() for _, l in created]
    for n, l in created:
        if not re.fullmatch(r"[a-z0-9_.\-]{1,63}", n):
            print(f"BAD  action name {n!r} is not a valid path component (lowercase, digits, - _ . only, < 64)")
            bad += 1
        if not (1 <= len(l) < 128):
            print(f"BAD  localized name {l!r} has a bad length")
            bad += 1
    for n in sorted({n for n in names if names.count(n) > 1}):
        print(f"BAD  action name {n!r} is used twice")
        bad += 1
    for l in sorted({l for l in locs if locs.count(l) > 1}):
        print(f"BAD  localized name {l!r} is used twice (XR_ERROR_LOCALIZED_NAME_DUPLICATED)")
        bad += 1

    # --- profile blocks
    order = []
    bound = set()
    frame_actions = set()
    for profile, body, _off in blocks(text):
        order.append(profile)
        hands, comps = PROFILES.get(profile, (None, None))
        if comps is None:
            print(f"note: {profile}: no component list here, skipped")
            continue
        per_comp_hands = {}
        for line in body.splitlines():
            line = line.split("//")[0]
            m = re.search(r'\{\s*xr\.(\w+(?:\[\d\])?)\s*,\s*path\("(/user/hand/(\w+)/(input|output)/([\w/]+))"\)\s*\}', line)
            if not m:
                continue
            action, path, hand, kind, comp = m.groups()
            comp = comp if kind == "input" else "output/" + comp
            bound.add(action)
            per_comp_hands.setdefault(comp, set()).add(hand)
            if profile == FRAME:
                frame_actions.add(action)
            if hand not in hands or comp not in comps:
                print(f"BAD  {profile}: {path} is not a component of that profile")
                bad += 1
            if profile == FRAME and comp in FRAME_HAND and FRAME_HAND[comp] != hand:
                print(f"BAD  {profile}: {path}: the Frame has {comp} on the {FRAME_HAND[comp]} controller")
                bad += 1
        if profile == FRAME:
            for comp in FRAME_BOTH:
                if per_comp_hands.get(comp, set()) != BOTH:
                    print(f"BAD  {profile}: {comp} must be bound on both hands, got {sorted(per_comp_hands.get(comp, []))}")
                    bad += 1
            for need in FRAME_NEEDS:
                if need not in frame_actions:
                    print(f"BAD  Frame profile does not bind {need}")
                    bad += 1

    if FRAME not in order:
        print("BAD  no Steam Frame profile block in vr_openxr.cpp")
        bad += 1
    elif order[0] != FRAME:
        print(f"BAD  the Frame profile must be suggested first, but {order[0]} comes before it")
        bad += 1
    for fallback in ("/interaction_profiles/valve/index_controller", "/interaction_profiles/oculus/touch_controller",
                     "/interaction_profiles/khr/simple_controller"):
        if fallback not in order:
            print(f"BAD  fallback profile {fallback} is not suggested")
            bad += 1

    # --- every created action is bound somewhere
    var_of = {}  # action name -> member, from "make_action("grip_pose", ..., &xr.grip_pose_action)"
    for m in re.finditer(r'make_(?:action|plain)\(\s*"([^"]+)"[^;]*?&xr\.(\w+(?:\[\d\])?)\s*\)', text, re.S):
        var_of[m.group(1)] = m.group(2)
    for n in names:
        member = var_of.get(n)
        if member is None:
            print(f"BAD  action {n!r}: cannot find the variable it is created into")
            bad += 1
        elif member not in bound:
            print(f"BAD  action {n!r} ({member}) is created but no profile binds it")
            bad += 1

    print(f"checked {len(order)} profiles, {len(created)} actions: {'FAILED' if bad else 'ok'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
