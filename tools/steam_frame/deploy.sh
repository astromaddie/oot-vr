#!/usr/bin/env bash
# Install, launch and feed OoT VR on a Valve Steam Frame (Android in the Lepton
# container) from a Mac or Linux host. See docs/STEAM_FRAME.md.
#
#   tools/steam_frame/deploy.sh [options]
#
#   --apk FILE         the frame-build APK (default: the newest assembleFrame output)
#   --rom FILE         copy your Ocarina of Time ROM into the headset's
#                      ~/Documents/OoTVR (over scp, SHA-1 printed). The game finds it
#                      there by content, whatever the file is called, and builds oot.o2r from it
#                      on the first start.
#   --register-title   register ~/devkit-game/OoTVR as a persistent Steam library
#                      title (once). When the title exists, every install also
#                      refreshes its copy of the APK.
#   --no-install       skip adb install
#   --no-launch        skip starting the game over adb
#   --logcat SECONDS   after launching, capture SECONDS of the game's log into
#                      scratchpad/frame-logcat.txt (and print the key lines)
#   --ssh-log LINES    print the last LINES of the game's own log file
#                      (~/Documents/OoTVR/logs/) over SSH; no adb needed
#   --diag KEY=VAL     set a key in ~/Documents/OoTVR/vr-diag.ini (repeatable).
#                      Keys: force_render, diag_yaw (deg), dump_frame (XR frame number),
#                      timing, autostart, autostart_entrance (e.g. 0x00EE).
#                      --diag-reset deletes the file.
#   --force-render     shorthand for --diag force_render=1 (draw and submit frames
#                      although the runtime says shouldRender=false: the headset on
#                      a desk). --no-force-render sets it to 0. Not for play.
#   --autostart        shorthand for --diag autostart=1: the title hands over to
#                      gameplay with the debug save, no controller needed.
#   --clear-dumps      remove ~/Documents/OoTVR/vr-dumps before the run
#   --pull-dumps DIR   after the run (and --logcat), adb pull vr-dumps into DIR
#                      (PNG copies are made with sips on a Mac)
#   --no-adb           only SSH steps (--rom, --register-title, the title's APK copy, --diag,
#                      --ssh-log); no Lepton Development container needed
#
# Environment:
#   FRAME_HOST       ssh target (default steamos@192.168.1.31, the Frame on Wi-Fi;
#                    over USB networking use steamos@10.86.200.233)
#   FRAME_ADB_HOST   adb host (default: the host part of FRAME_HOST). Lepton
#                    Development publishes its adbd on <host>:5555
#   ADB              adb binary
#
# adb needs "Lepton Development" running (start it from the Frame's Steam
# library). It deletes its apps and their data when it exits; the persistent
# Steam title (--register-title) and the ROM in ~/Documents/OoTVR survive.
# A binary started by adb gets no controllers and no focus from SteamVR: use it
# for logs, and launch the Steam library title to play.
#
# Check first that nobody else is using the headset (SteamVR runs one scene app at a time):
# ssh to the Frame and look at `pgrep -af lepton` and the last minute of the journal.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
package="com.linkzenic.soh.frame"
activity="com.dishii.soh.MainActivity"
title="OoTVR"                       # Steam devkit titles must be alphanumeric
title_apk="OoT-VR.apk"              # fixed name: the title's argv stays valid across builds
data_dir="Documents/OoTVR"          # on the headset; /sdcard/Documents/OoTVR inside Lepton

frame_host="${FRAME_HOST:-steamos@192.168.1.31}"
adb_host="${FRAME_ADB_HOST:-${frame_host#*@}}"
adb_bin="${ADB:-${ANDROID_HOME:-$HOME/Library/Android/sdk}/platform-tools/adb}"
[[ -x "$adb_bin" ]] || adb_bin="$(command -v adb || true)"
apk=""
rom=""
register=0
install=1
launch=1
use_adb=1
logcat_seconds=0
ssh_log_lines=0
diag=()             # KEY=VAL for vr-diag.ini
diag_reset=0
clear_dumps=0
pull_dumps=""

die() { echo "deploy.sh: $*" >&2; exit 1; }

while (($#)); do
    case "$1" in
        --apk) apk="$2"; shift 2 ;;
        --rom) rom="$2"; shift 2 ;;
        --register-title) register=1; shift ;;
        --no-install) install=0; shift ;;
        --no-launch) launch=0; shift ;;
        --logcat) logcat_seconds="$2"; shift 2 ;;
        --ssh-log) ssh_log_lines="$2"; shift 2 ;;
        --diag) diag+=("$2"); shift 2 ;;
        --diag-reset) diag_reset=1; shift ;;
        --force-render) diag+=("force_render=1"); shift ;;
        --no-force-render) diag+=("force_render=0"); shift ;;
        --autostart) diag+=("autostart=1"); shift ;;
        --clear-dumps) clear_dumps=1; shift ;;
        --pull-dumps) pull_dumps="$2"; shift 2 ;;
        --no-adb) use_adb=0; shift ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d'; exit 0 ;;
        *) die "unknown option $1" ;;
    esac
done

if [[ -z "$apk" ]]; then
    # the build names it soh_frame_v<version>-frame.apk; take the newest
    apk="$(ls -t "$root"/Android/app/build/outputs/apk/frame/*.apk 2>/dev/null | head -1 || true)"
fi

ssh_frame() { ssh -o ConnectTimeout=10 -o BatchMode=yes "$frame_host" "$@"; }

# ---- ROM: persistent storage on the Frame host -------------------------------
if [[ -n "$rom" ]]; then
    [[ -f "$rom" ]] || die "no such ROM: $rom"
    sum="$(shasum -a 1 "$rom" | cut -d' ' -f1)"
    name="$(printf '%s' "$(basename "$rom")" | tr -c 'A-Za-z0-9._-' '_')"
    ssh_frame "mkdir -p ~/$data_dir && chmod 777 ~/$data_dir"
    scp -q "$rom" "$frame_host:$data_dir/$name.part"
    ssh_frame "mv ~/$data_dir/$name.part ~/$data_dir/$name && chmod 666 ~/$data_dir/$name"
    remote_sum="$(ssh_frame "sha1sum ~/$data_dir/$name" | cut -d' ' -f1)"
    [[ "$remote_sum" == "$sum" ]] || die "ROM copy differs on the Frame ($remote_sum)"
    echo "ROM in place: ~/$data_dir/$name (/sdcard/$data_dir/$name inside Lepton), SHA-1 $sum"
    echo "(the game checks the ROM itself; supported versions are listed at https://ship.equipment/)"
fi

# ---- Unattended-test switches (files in the data folder) ----------------------
if ((diag_reset)); then
    ssh_frame "rm -f ~/$data_dir/vr-diag.ini"
    echo "vr-diag.ini removed"
fi
if ((${#diag[@]})); then
    # edit vr-diag.ini in place: replace a key, or append it
    ssh_frame "mkdir -p ~/$data_dir && python3 - ~/$data_dir/vr-diag.ini ${diag[*]}" <<'PY'
import os, sys
path, pairs = sys.argv[1], sys.argv[2:]
lines = open(path).read().splitlines() if os.path.exists(path) else []
for pair in pairs:
    key, _, val = pair.partition("=")
    for i, line in enumerate(lines):
        if line.split("=")[0].strip() == key:
            lines[i] = "%s = %s" % (key, val)
            break
    else:
        lines.append("%s = %s" % (key, val))
open(path, "w").write("\n".join(lines) + "\n")
os.chmod(path, 0o666)
print("vr-diag.ini:", " ".join(lines))
PY
fi
if ((clear_dumps)); then
    ssh_frame "rm -rf ~/$data_dir/vr-dumps"
fi

if ((install || register)); then
    [[ -n "$apk" && -f "$apk" ]] || die "no APK found (build it: cd Android && sh ./gradlew assembleFrame)"
fi

# ---- Persistent Steam title --------------------------------------------------
# An existing title always gets the new APK; --register-title creates it.
title_exists=0
ssh_frame "test -d ~/devkit-game/$title" 2>/dev/null && title_exists=1
if ((register || (title_exists && install))); then
    ssh_frame "mkdir -p ~/devkit-game/$title"
    scp -q "$apk" "$frame_host:devkit-game/$title/$title_apk"
    echo "copied the APK into ~/devkit-game/$title/$title_apk"
fi
if ((register)); then
    parms="{\"gameid\": \"$title\", \"directory\": \"/home/steamos/devkit-game/$title\", \"force_appid\": \"\", \"argv\": [\"$title_apk\"], \"env\": {}, \"settings\": {\"steam_play\": \"1\", \"compat_tool\": \"lepton\"}, \"lepton_args\": \"\"}"
    # needs the Steam client running on the Frame; it answers with success or an error
    ssh_frame "cd ~/devkit-utils && python3 steam-client-create-shortcut --parms '$parms'"
fi

if ((ssh_log_lines > 0)); then
    ssh_frame "ls -t ~/$data_dir/logs/*.log 2>/dev/null | head -1 | xargs -r tail -n $ssh_log_lines" || true
fi

((use_adb)) || exit 0
[[ -n "$adb_bin" && -x "$adb_bin" ]] || die "adb not found; set ADB"

# ---- Lepton Development over network adb --------------------------------------
"$adb_bin" start-server >/dev/null
for attempt in 1 2 3 4 5; do
    "$adb_bin" connect "$adb_host:5555" >/dev/null 2>&1 || true
    if "$adb_bin" -s "$adb_host:5555" shell true 2>/dev/null; then break; fi
    sleep $((attempt * 2))
    if ((attempt == 5)); then
        die 'cannot reach Lepton Development at '"$adb_host"':5555. Start "Lepton Development" from the Frame'"'"'s Steam library (check: ssh '"$frame_host"' pgrep -af lepton).'
    fi
done
device="$adb_host:5555"
adb() { "$adb_bin" -s "$device" "$@"; }

if ((install)); then
    adb install -r -g "$apk" | grep -i -E 'success|fail|error' || true
    # The data folder is /sdcard/Documents/OoTVR. Lepton 2.8.14 does not enforce all-files
    # access there (it is a symlink to the host's ~/Documents); grant it anyway if it is asked for.
    adb shell appops set "$package" MANAGE_EXTERNAL_STORAGE allow >/dev/null 2>&1 || true
fi

if ((launch)); then
    # A running copy of ours is restarted; nothing else is touched.
    adb shell am force-stop "$package"
    adb logcat -c
    adb shell am start -n "$package/$activity" >/dev/null
    echo "started $package; follow it with: $adb_bin -s $device logcat -s SoH SDL/APP"
fi

if ((logcat_seconds > 0)); then
    out="$root/scratchpad/frame-logcat.txt"
    mkdir -p "$(dirname "$out")"
    timeout_bin="$(command -v timeout || command -v gtimeout || true)"
    if [[ -n "$timeout_bin" ]]; then
        "$timeout_bin" "$logcat_seconds" "$adb_bin" -s "$device" logcat -v time >"$out" || true
    else
        ("$adb_bin" -s "$device" logcat -v time >"$out" & pid=$!; sleep "$logcat_seconds"; kill "$pid") 2>/dev/null || true
    fi
    echo "log: $out"
    grep -E "\[vr\]|\[VR\]|\[vr-perf\]|\[vr-dump\]|\[vr-diag\]|\[vr-sys\]|\[VR_CTRL\]|setupFiles|\[Setup\]|SoH" "$out" | head -120 || true
fi

if [[ -n "$pull_dumps" ]]; then
    mkdir -p "$pull_dumps"
    "$adb_bin" -s "$device" pull /sdcard/$data_dir/vr-dumps/. "$pull_dumps" >/dev/null
    if command -v sips >/dev/null; then
        for bmp in "$pull_dumps"/*.bmp; do
            [[ -f "$bmp" ]] && sips -s format png "$bmp" --out "${bmp%.bmp}.png" >/dev/null
        done
    fi
    echo "dumps in $pull_dumps: $(ls "$pull_dumps" | tr '\n' ' ')"
fi
