#!/usr/bin/env bash
# Plays tests/full_run.txt against a built game in a virtual X display and
# checks that the run reaches the win screen and the level reset.
#
#   tests/run_playtest.sh [build-dir]     (default: build)
#
# Needs xvfb-run. Uses its own save file location (the build dir), which it
# clears first, so do not point it at a directory with a save you care about.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build}"
game="$build/spore-lantern"
log="$(mktemp)"

[ -x "$game" ] || { echo "no game binary at $game (build first)"; exit 2; }
rm -f "$build/save.txt"

SPORE_MUTE=1 SPORE_SPEED=4 SPORE_SCRIPT="$root/tests/full_run.txt" \
    xvfb-run -a -s "-screen 0 960x540x24" "$game" > "$log" 2>&1 || true

status=0
for expected in "Spore collected (3/3)" "Saved to save.txt" "Potion brewed" "Potion thrown" \
                "Slime defeated" "Door open" "WON in" "Level reset after win"; do
    if grep -qF "$expected" "$log"; then
        echo "ok    $expected"
    else
        echo "FAIL  $expected"
        status=1
    fi
done
if grep -qF "Hurt" "$log"; then echo "note  the scripted run took damage"; fi
rm -f "$log" "$build/save.txt"
exit $status
