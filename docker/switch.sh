#!/usr/bin/env bash
# Builds Equinox.nro inside the devkitPro container, the same way CI does.
# The build tree lives in a Docker volume (/build) because bind mounts on
# Windows are slow; only the final files are copied back to the repo.
set -euo pipefail

SRC=/src
BUILD=/build/switch
OUT="$SRC/build/docker"
JOBS="${JOBS:-$(nproc)}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

git config --global --add safe.directory '*'

# The Switch build uses its own enet; the devkitPro package conflicts with it.
if dkp-pacman -Q switch-enet >/dev/null 2>&1; then
    dkp-pacman --noconfirm -R switch-enet
fi

if [ ! -f "$SRC/extern/borealis/library/CMakeLists.txt" ]; then
    echo "Submodules are missing. Run: git submodule update --init --recursive" >&2
    exit 1
fi

configure() {
    cmake -S "$SRC" -B "$BUILD" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DPLATFORM_SWITCH=ON \
        -DUSE_DEKO3D=ON
}

# The first configure of a clean tree fails on a generated file and the
# second one succeeds; CI runs it twice for the same reason.
configure || configure

make -C "$BUILD" Moonlight.nro -j"$JOBS"

mkdir -p "$OUT"
cp "$BUILD/Moonlight.nro" "$OUT/Equinox.nro"
cp "$BUILD/Moonlight.elf" "$OUT/Equinox.elf"
echo "Done: build/docker/Equinox.nro"
