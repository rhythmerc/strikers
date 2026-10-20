#!/bin/sh
# Mixed reality on the desktop: the Quest build's OpenXR path (port/xr.h, Aurora's lib/xr) against
# Monado's simulated headset, so 3D changes can be checked without a headset.
#
# Usage:
#   tools/xr-desktop.sh build                  configure and build build/linux-xr
#   tools/xr-desktop.sh run [VAR=value...]      play, in Monado's window
#   tools/xr-desktop.sh shot NAME [VAR=value...]
#                                               a benchmark match, unattended: the eye images
#                                               (both eyes, stacked), the HUD and the screen
#                                               after 5 s of 3D, as PNGs in
#                                               build/linux-xr/shots/NAME/dump
#
# Needs the melee-xr distrobox from ~/projects/xr-toolchain (Monado, the OpenXR loader and the
# NVIDIA Vulkan driver), with clang-18, lld-18, llvm-18 and libavcodec-dev added, and a checkout
# of Dawn's melee-xr branch at ~/projects/dawn (or STRIKERS_DAWN_SOURCE), as tools/android/build.sh
# does. Monado starts in the background (xr-sim) when it isn't running, and is left running.
# Keys for the simulated headset are at the top of ~/projects/xr-toolchain/bin/xr-sim; click into
# Monado's debug window first.
#
# Settings go on the command line as VAR=value (STRIKERS_FORCE_STADIUM=2, AURORA_XR_ARENA_POS=...)
# or in build/linux-xr/run/strikers.ini, as on the headset.

set -e

PORT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BOX="${STRIKERS_XR_BOX:-melee-xr}"
TOOLCHAIN="${XR_TOOLCHAIN:-$HOME/projects/xr-toolchain}"
BUILD="$PORT/build/linux-xr"
DISC="${STRIKERS_DATA:-$HOME/Downloads/strikers-iso/G4QE01.iso}"

# Everything runs in the box; re-enter it from the host.
if [ ! -e /run/.containerenv ] && [ -z "${CONTAINER_ID:-}" ]; then
    exec distrobox enter "$BOX" -- sh "$PORT/tools/xr-desktop.sh" "$@"
fi

. "$TOOLCHAIN/env.sh"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export PATH="/usr/lib/llvm-18/bin:$PATH"   # llvm-nm for tools/genstubs.py

start_monado()
{
    if pgrep -x monado-service >/dev/null; then
        return
    fi
    echo "==> starting Monado (xr-sim; stop it with $TOOLCHAIN/bin/xr-sim stop)"
    # A socket left by a service that was killed: with none running, nothing is listening on it.
    rm -f "${XDG_RUNTIME_DIR:-/tmp}/monado_comp_ipc"
    nohup "$TOOLCHAIN/bin/xr-sim" >"${XDG_RUNTIME_DIR:-/tmp}/xr-sim.log" 2>&1 &
    i=0
    while [ ! -S "${XDG_RUNTIME_DIR:-/tmp}/monado_comp_ipc" ] && [ $i -lt 50 ]; do
        sleep 0.2
        i=$((i + 1))
    done
}

# The game stalls at the memory card prompt on an empty card, so the first run makes a save: four
# presses of A through the prompts, then the run ends.
seed_save()
{
    if [ -d "$BUILD/seed/USA" ]; then
        return
    fi
    echo "==> making a save to start from (first run only)"
    rm -rf "$BUILD/seed"
    mkdir -p "$BUILD/seed"
    STRIKERS_USER_DIR="$BUILD/seed" STRIKERS_CACHE_DIR="$BUILD/cache" STRIKERS_DATA="$DISC" \
        STRIKERS_AUDIO=0 STRIKERS_NO_MESSAGEBOX=1 STRIKERS_AUTOPRESS='A@300,A@450,A@600,A@750' \
        timeout 40 "$BUILD/strikers" >"$BUILD/seed/out.log" 2>&1 || true
    if [ ! -d "$BUILD/seed/USA" ]; then
        echo "error: no save was made; see $BUILD/seed/out.log" >&2
        exit 1
    fi
}

# Runs the game in $1 (a run directory with the save in it) with the rest as extra environment.
run_game()
{
    dir=$1
    shift
    env STRIKERS_DATA="$DISC" STRIKERS_USER_DIR="$dir" STRIKERS_CACHE_DIR="$BUILD/cache" \
        STRIKERS_CONFIG="$dir/strikers.ini" STRIKERS_NO_MESSAGEBOX=1 AURORA_XR=1 \
        "$@" "$BUILD/strikers"
}

ensure_ini()
{
    [ -f "$1/strikers.ini" ] || cat >"$1/strikers.ini" <<'EOF'
# Desktop mixed reality (tools/xr-desktop.sh). Same keys as the headset's strikers.ini.
# xr_figure_scale = 1.5
# force_stadium = 0
EOF
}

case "${1:-}" in
build)
    if [ ! -f "$BUILD/build.ninja" ]; then
        DAWN="${STRIKERS_DAWN_SOURCE:-$HOME/projects/dawn}"
        if ! grep -q SetExternalVulkanHooks "$DAWN/include/dawn/native/VulkanBackend.h" 2>/dev/null; then
            echo "error: $DAWN is not a melee-xr Dawn checkout (set STRIKERS_DAWN_SOURCE)" >&2
            exit 1
        fi
        echo "==> configure ($BUILD, Dawn $DAWN)"
        cmake -S "$PORT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
            -DAURORA_SDL3_PROVIDER=vendor -DAURORA_ENABLE_OPENXR=ON \
            -DAURORA_DAWN_PROVIDER=vendor -DFETCHCONTENT_SOURCE_DIR_DAWN="$DAWN" \
            -DDAWN_SUPPORTS_CXX_MODULES=OFF -DDAWN_BUILD_PROTOBUF=OFF -DTINT_BUILD_IR_BINARY=OFF \
            >"$BUILD.configure.log" 2>&1 || { tail -40 "$BUILD.configure.log" >&2; exit 1; }
    fi
    cd "$PORT"
    sh tools/rebuild.sh "$BUILD"
    ;;
run)
    shift
    start_monado
    seed_save
    mkdir -p "$BUILD/run"
    [ -d "$BUILD/run/USA" ] || cp -r "$BUILD/seed/USA" "$BUILD/run/"
    ensure_ini "$BUILD/run"
    run_game "$BUILD/run" STRIKERS_LOG=console "$@"
    ;;
shot)
    shift
    name=${1:?usage: tools/xr-desktop.sh shot NAME [VAR=value...]}
    shift
    start_monado
    seed_save
    dir="$BUILD/shots/$name"
    rm -rf "$dir"
    mkdir -p "$dir/dump"
    cp -r "$BUILD/seed/USA" "$dir/"
    ensure_ini "$dir"
    # The benchmark's demo match, at a fixed step and seed so a shot repeats. Its intro cut-scene
    # stays on the screen (about 50 s); the 3D eyes are dumped once they've released
    # AURORA_XR_DUMP_AFTER images (300: 5 s of match in 3D), and the run ends there. The pitch
    # stands a metre out and half a metre under the simulated eyes, which look straight ahead.
    run_game "$dir" STRIKERS_BENCHMARK=1 STRIKERS_FIXED_DT=1 STRIKERS_SEED=1 \
        AURORA_XR_DUMP="$dir/dump" AURORA_XR_DUMP_AFTER=300 AURORA_XR_ARENA_POS=0,-0.5,-1.0 "$@" \
        timeout "${STRIKERS_XR_SHOT_SECONDS:-150}" >"$dir/out.log" 2>&1 &
    game=$!
    while kill -0 $game 2>/dev/null && ! grep -q "Dumped 3d" "$dir/out.log"; do
        sleep 1
    done
    i=0   # the HUD's comes a moment later, when there is one
    while [ $i -lt 5 ] && kill -0 $game 2>/dev/null && ! grep -q "Dumped hud" "$dir/out.log"; do
        sleep 1
        i=$((i + 1))
    done
    # The game's teardown under a signal can fault; the shot is already on disk.
    pkill -KILL -f "^$BUILD/strikers" 2>/dev/null || true
    wait $game 2>/dev/null || true
    if [ ! -f "$dir/dump/xr_3d.ppm" ]; then
        echo "error: no 3D frame was dumped; see $dir/out.log" >&2
        exit 1
    fi
    python3 -I "$PORT/tools/ppm2png.py" "$dir/dump"/*.ppm
    rm -f "$dir/dump"/*.ppm "$dir/dump"/*.pgm
    ls "$dir/dump"
    ;;
*)
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
