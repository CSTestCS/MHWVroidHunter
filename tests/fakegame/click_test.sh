#!/usr/bin/env bash
# Regression test for the "clicking in the F7 menu freezes the game" bug.
# Runs the stand-in game under Wine + Xvfb with the menu open, clicks several controls and
# checks that the game keeps running (a deadlock makes it hit the timeout instead).
#   tests/fakegame/click_test.sh <folder with MonsterHunterWorld.exe (fakegame), dinput8.dll, nativePC/plugins/...>
set -euo pipefail
G="$1"
export DISPLAY=${DISPLAY:-:99} WINEDEBUG=-all WINEDLLOVERRIDES="dinput8=n,b" VH_OPEN_MENU_ON_START=1
pgrep Xvfb >/dev/null || (Xvfb "$DISPLAY" -screen 0 1280x800x24 >/dev/null 2>&1 &)
LOG=$(mktemp)
(cd "$G" && timeout 120 wine64 MonsterHunterWorld.exe 900 shot.bmp >"$LOG" 2>&1; echo "exit=$?" >>"$LOG") &
for _ in $(seq 60); do W=$(xdotool search --name "fake" 2>/dev/null | head -1 || true); [[ -n "$W" ]] && break; sleep 1; done
sleep 5
for xy in "78 104" "80 310" "200 210" "600 600" "78 104"; do xdotool mousemove --window "$W" $xy click 1; sleep 1; done
until grep -q "exit=" "$LOG"; do sleep 2; done
cat "$LOG"
grep -q "exit=0" "$LOG" && echo "PASS: game kept running after clicks" || { echo "FAIL: game froze"; exit 1; }
