#!/usr/bin/env bash
# Plays tests/full_run.txt against the web build in headless Chromium and
# checks the same milestones as run_playtest.sh.
#
#   tests/run_web_playtest.sh [build-dir]     (default: build-web)
#
# Needs python3 (to serve the files) and a Chromium binary: set CHROME, or it
# looks for chrome-headless-shell from a Playwright install, then chromium.
# Each run uses a fresh browser profile, so no save is carried over.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build-web}"
[ -f "$build/spore-lantern.html" ] || { echo "no web build at $build (build first)"; exit 2; }

chrome="${CHROME:-}"
if [ -z "$chrome" ]; then
    chrome="$(ls -d "$HOME"/.cache/ms-playwright/chromium_headless_shell-*/chrome-headless-shell-linux64/chrome-headless-shell 2>/dev/null | tail -1 || true)"
fi
[ -n "$chrome" ] || chrome="$(command -v chromium || command -v chromium-browser || command -v google-chrome || true)"
[ -n "$chrome" ] || { echo "no Chromium found (set CHROME)"; exit 2; }

work="$(mktemp -d)"
log="$work/console.log"
port=8799
cp "$root/tests/full_run.txt" "$build/full_run.txt"
python3 -m http.server "$port" --bind 127.0.0.1 --directory "$build" > /dev/null 2>&1 &
server=$!
trap 'kill $server 2>/dev/null; rm -rf "$work" "$build/full_run.txt"' EXIT
sleep 1

# Runs until the script's final "quit" step has had time to happen; the page
# cannot close itself, so the browser is stopped by the timeout.
url="http://127.0.0.1:$port/spore-lantern.html?env=SPORE_MUTE=1,SPORE_SPEED=4&script=full_run.txt"
timeout "${WEB_PLAYTEST_SECONDS:-60}" "$chrome" --headless --no-sandbox \
    --user-data-dir="$work/profile" --window-size=960,700 \
    --use-angle=swiftshader --enable-unsafe-swiftshader \
    --enable-logging=stderr --v=0 "$url" > "$log" 2>&1 || true

status=0
for expected in "Spore collected (3/3)" "Saved to /persist/save.txt" "Potion brewed" "Potion thrown" \
                "Slime defeated" "Door open" "WON in" "Level reset after win"; do
    if grep -qF "$expected" "$log"; then
        echo "ok    $expected"
    else
        echo "FAIL  $expected"
        status=1
    fi
done
if grep -qF "Hurt" "$log"; then echo "note  the scripted run took damage"; fi
grep -oE "OpenGL version: [^\"]*" "$log" | head -1 | sed 's/^/info  /' || true
exit $status
