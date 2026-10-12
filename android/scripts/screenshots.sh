#!/usr/bin/env bash
# Renders the main screens to PNGs (Roborazzi on Robolectric) to look at. Opt-in and local
# only: never part of CI or `./gradlew test`. Runs through scripts/gradle.sh (memory cap, low priority, no daemons
# left behind).
#
#   scripts/screenshots.sh            # all screens -> app/build/screenshots/*.png
#   scripts/screenshots.sh homeDark   # one test method
set -euo pipefail
cd "$(dirname "$0")/.."
filter=(--tests "*ScreenshotTest*")
[[ -n "${1:-}" ]] && filter=(--tests "*ScreenshotTest.$1")
rm -rf app/build/screenshots
scripts/gradle.sh -Pscreenshots :app:testDebugUnitTest "${filter[@]}" --rerun-tasks -q
ls -1 app/build/screenshots
