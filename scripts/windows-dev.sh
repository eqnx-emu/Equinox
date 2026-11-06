#!/usr/bin/env bash
# Local Windows build (MSYS2 UCRT64), same steps as the Windows CI job.
# Run from any shell: C:/msys64/usr/bin/bash.exe scripts/windows-dev.sh [--run]
set -euo pipefail

export MSYSTEM=UCRT64
export PATH="/ucrt64/bin:/usr/bin:$PATH"
export TMP="$(cygpath -w /tmp)" TEMP="$(cygpath -w /tmp)"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/windows-ucrt64"
cd "$ROOT"

if [ ! -f "$BUILD/build.ninja" ] || [ ! -f "$BUILD/CMakeCache.txt" ]; then
    rm -rf "$BUILD/CMakeCache.txt" "$BUILD/CMakeFiles"
    cmake --preset windows-ucrt64-release
fi
cmake --build --preset windows-ucrt64-release --parallel

# The build itself copies the MSYS2 runtime DLLs next to the executable.
echo "Built: build/windows-ucrt64/Moonlight.exe"

if [ "${1:-}" = "--run" ]; then
    cd "$BUILD" && ./Moonlight.exe &
fi
