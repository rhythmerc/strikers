#!/bin/sh
# Meta Quest build: libstrikers.so through the NDK, then a debug APK.
# Usage: tools/android/build.sh [build-dir]   (default build/android-arm64)
#
# Needs an Android SDK (ANDROID_HOME, default ~/Android) with an NDK r28 or newer, a JDK 17, and a
# checkout of Dawn's melee-xr branch (github.com/rhythmerc/dawn): Aurora's OpenXR presentation
# renders into the runtime's swapchain images on Dawn's own device, which stock Dawn cannot do.

set -e

PORT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$PORT"
BUILD="${1:-build/android-arm64}"
ANDROID_DIR="$PORT/platforms/android"

export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android}"
if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    ANDROID_NDK_HOME=$(find "$ANDROID_HOME/ndk" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | sort -V | tail -1)
fi
if [ ! -f "$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" ]; then
    echo "error: no Android NDK; set ANDROID_NDK_HOME" >&2
    exit 1
fi
# Dawn from source needs C++20 aggregate CTAD, which r26's clang 17 rejects.
NDK_MAJOR=$(sed -n 's/^Pkg.Revision *= *\([0-9]*\).*/\1/p' "$ANDROID_NDK_HOME/source.properties")
if [ "$NDK_MAJOR" -lt 28 ]; then
    echo "error: $ANDROID_NDK_HOME is NDK r$NDK_MAJOR; Dawn needs r28 or newer" >&2
    exit 1
fi
export ANDROID_NDK_HOME
if [ -z "${JAVA_HOME:-}" ] && [ -d /usr/lib/jvm/java-17-openjdk-amd64 ]; then
    export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
fi
NDK_BIN=$(dirname "$(find "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt" -name llvm-nm -print -quit)")

# The Dawn the XR build needs. STRIKERS_DAWN_SOURCE, else ~/projects/dawn, else the pinned commit
# cloned into the build directory.
DAWN_REPO="${STRIKERS_DAWN_REPO:-https://github.com/rhythmerc/dawn.git}"
DAWN_REF="${STRIKERS_DAWN_REF:-343ff6c62ea5e0f23f3a83858fea725e04479e31}"
if [ -z "${STRIKERS_DAWN_SOURCE:-}" ]; then
    if [ -d "$HOME/projects/dawn/.git" ]; then
        STRIKERS_DAWN_SOURCE="$HOME/projects/dawn"
    else
        STRIKERS_DAWN_SOURCE="$PORT/build/dawn-src"
        if [ ! -d "$STRIKERS_DAWN_SOURCE/.git" ]; then
            echo "==> fetching Dawn ($DAWN_REPO @ $DAWN_REF)"
            git init -q "$STRIKERS_DAWN_SOURCE"
            git -C "$STRIKERS_DAWN_SOURCE" fetch -q --depth 1 "$DAWN_REPO" "$DAWN_REF"
            git -C "$STRIKERS_DAWN_SOURCE" checkout -q FETCH_HEAD
            (cd "$STRIKERS_DAWN_SOURCE" && python3 tools/fetch_dawn_dependencies.py)
        fi
    fi
fi
if ! grep -q SetExternalVulkanHooks "$STRIKERS_DAWN_SOURCE/include/dawn/native/VulkanBackend.h" 2>/dev/null; then
    echo "error: $STRIKERS_DAWN_SOURCE is not a melee-xr Dawn checkout" >&2
    exit 1
fi

LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT INT TERM

echo "==> configure ($BUILD, NDK r$NDK_MAJOR, Dawn $STRIKERS_DAWN_SOURCE)"
# cortex-a78c: all six cores of the Quest 3's XR2 Gen 2 (CPU part 0xd4b).
cmake -S . -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29 \
    -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-z,max-page-size=16384" \
    -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON \
    -DSTRIKERS_TARGET_CPU="${STRIKERS_TARGET_CPU:-cortex-a78c}" \
    -DAURORA_SDL3_PROVIDER=vendor \
    -DAURORA_ENABLE_OPENXR=ON \
    -DAURORA_DAWN_PROVIDER=vendor \
    -DFETCHCONTENT_SOURCE_DIR_DAWN="$STRIKERS_DAWN_SOURCE" \
    -DDAWN_SUPPORTS_CXX_MODULES=OFF \
    -DDAWN_BUILD_PROTOBUF=OFF \
    -DTINT_BUILD_IR_BINARY=OFF \
    >"$LOG" 2>&1 || { tail -40 "$LOG" >&2; exit 1; }

echo "==> compile"
# Expected to fail at the link on a clean tree: the stubs do not exist yet.
set +e
cmake --build "$BUILD" --target strikers -- -k 0 -j "${JOBS:-8}" >"$LOG" 2>&1
STATUS=$?
set -e
DIAG='\.(c|cc|cpp|h|hpp)[:(][0-9]+[:,][0-9]+\)?: (fatal )?error:'
if grep -qE "$DIAG" "$LOG"; then
    echo "==> COMPILE FAILED" >&2
    grep -E "$DIAG" "$LOG" | head -20 >&2
    exit 1
fi
OTHER=$(sed -n 's/^FAILED: \(\[code=[0-9]*\] \)\{0,1\}//p' "$LOG" \
    | tr -d '\r' | sed 's/[[:space:]]*$//' | grep -vxE 'libstrikers\.so' || true)
if [ -n "$OTHER" ] || { [ "$STATUS" -ne 0 ] && ! grep -q 'undefined symbol' "$LOG"; }; then
    echo "==> BUILD FAILED before stub generation" >&2
    tail -40 "$LOG" >&2
    exit 1
fi

echo "==> generate stubs"
# The NDK's llvm-nm: under STRIKERS_LTO the objects are bitcode from the NDK's clang, newer than
# any host LLVM may read.
PATH="$NDK_BIN:$PATH" STRIKERS_SYMBOL_PREFIX= STRIKERS_BUILD_DIR="$BUILD" python3 tools/genstubs.py \
    --noop "^GX|^snd|^AI|^AR" --out "$BUILD/stubs_generated.c"

echo "==> link"
set +e
cmake --build "$BUILD" --target strikers -- -j "${JOBS:-8}" >"$LOG" 2>&1
STATUS=$?
set -e
if [ "$STATUS" -ne 0 ]; then
    echo "==> LINK FAILED" >&2
    grep -E 'error|undefined symbol' "$LOG" | head -30 >&2
    exit 1
fi

echo "==> stage native libraries"
JNI="$ANDROID_DIR/app/src/main/jniLibs/arm64-v8a"
rm -rf "$JNI"
mkdir -p "$JNI"
"$NDK_BIN/llvm-strip" --strip-unneeded -o "$JNI/libstrikers.so" "$BUILD/libstrikers.so"
LOADER=$(find "$BUILD/_deps" -path '*android.arm64-v8a/libopenxr_loader.so' -print -quit)
if [ -z "$LOADER" ]; then
    echo "error: no libopenxr_loader.so under $BUILD/_deps" >&2
    exit 1
fi
cp "$LOADER" "$JNI/"

echo "==> APK"
SDL_JAVA=$(sed -n 's/^AURORA_SDL3_JAVA_SOURCE_DIR:INTERNAL=//p' "$BUILD/CMakeCache.txt")
AURORA_JAVA=$(sed -n 's/^AURORA_ANDROID_JAVA_SOURCE_DIR:INTERNAL=//p' "$BUILD/CMakeCache.txt")
(cd "$ANDROID_DIR" && ./gradlew --no-daemon -q :app:assembleDebug \
    -Pstrikers.sdlJava="$SDL_JAVA" -Pstrikers.auroraJava="$AURORA_JAVA") >"$LOG" 2>&1 \
    || { tail -40 "$LOG" >&2; exit 1; }
cp "$ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk" "$BUILD/strikers-quest-debug.apk"
echo "==> ok: $BUILD/strikers-quest-debug.apk"
