#!/usr/bin/env bash
# Run Gradle without putting a shared build host at risk.
# Gradle, the Kotlin daemon and Robolectric together can reach ~7 GB and push a
# shared host out of memory.
#
# - memory-capped systemd scope: a build that grows too large is killed on its
#   own instead of the kernel picking host services (GRADLE_MEM_MAX, default 5G)
# - low CPU and IO priority
# - no daemons left behind: --no-daemon and the Kotlin compiler in-process
#
#   scripts/gradle.sh :app:testDebugUnitTest :app:lintDebug :app:assembleDebug
set -euo pipefail
cd "$(dirname "$0")/.."
mem=${GRADLE_MEM_MAX:-5G}
exec systemd-run --user --scope --quiet --collect \
  -p MemoryMax="$mem" -p MemorySwapMax=0 \
  nice -n 10 ionice -c 3 bash -c '
    status=0
    ./gradlew --no-daemon --max-workers=2 -Pkotlin.compiler.execution.strategy=in-process "$@" || status=$?
    cg=/sys/fs/cgroup$(cut -d: -f3 /proc/self/cgroup)
    peak=$(cat "$cg/memory.peak" 2>/dev/null || echo 0)
    echo "gradle.sh: peak memory $((peak / 1048576)) MiB (cap '"$mem"')" >&2
    exit $status' gradle "$@"
