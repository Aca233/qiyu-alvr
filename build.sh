#!/usr/bin/env bash
#
# build.sh — One-shot build for the ALVR Qiyu Dream client.
#
# What it does:
#   1. Clones ALVR v20.14.1 (pinned, so the C ABI always matches this client).
#   2. Builds alvr_client_core for aarch64 (the Rust streaming/decoding core).
#   3. Generates alvr_client_core.h with cbindgen (exact ABI the C++ bridges to).
#   4. Copies the .so + header into build/alvr_client_core/ where CMake/gradle expect them.
#   5. Prints the KEY generated ABI tokens to the log (so a CI failure is easy to debug).
#
# The final APK is built by Gradle (run `gradle assembleRelease`, or just push to
# GitHub and let the Actions workflow build it). This script only produces the
# native core + header that the Gradle/CMake build links against.
#
# Prerequisites (install once, for LOCAL builds):
#   - Android SDK with NDK r25 (ANDROID_NDK / ANDROID_NDK_HOME set)
#   - Rust + cargo (rustup), plus: rustup target add aarch64-linux-android
#   - cargo install cargo-ndk cbindgen
#   - JDK 17
#
set -euo pipefail

ALVR_TAG="v20.14.1"
ALVR_REPO="https://github.com/alvr-org/ALVR.git"
NDK_TARGET="arm64-v8a"
MIN_SDK=26
NDK_VERSION="25.1.8937393"
CBINDGEN_VER="0.26.0"
CARGO_NDK_VER="4.1.2"
SO_ABI="arm64-v8a"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$ROOT/build/alvr_client_core"

# ---- Resolve the NDK directory robustly ----
# android-actions/setup-android can point ANDROID_HOME straight at the ndk dir, which
# would otherwise produce a doubled ".../ndk/<ver>/ndk/<ver>" path and break cargo-ndk.
# We detect a real NDK (by its source.properties), strip any doubled suffix, and fall
# back to the standard runner location. We also force ANDROID_NDK_ROOT to match so
# cargo-ndk's "doesn't match ANDROID_NDK_ROOT" error goes away.
resolve_ndk() {
  local ver="$1"
  # Normalize a likely SDK root (strip a trailing /ndk/<ver> if present).
  local sdk="${ANDROID_HOME:-/usr/local/lib/android/sdk}"
  case "$sdk" in
    */ndk/*) sdk="$(echo "$sdk" | sed -E 's#(/ndk/[^/]+).*#\1#')" ;;
  esac
  local candidate
  # 1. The NDK version we explicitly installed via sdkmanager (preferred).
  candidate="$(ls -d "$sdk"/ndk/"$ver" 2>/dev/null | head -1)"
  [ -n "$candidate" ] && [ -f "$candidate/source.properties" ] && { echo "$candidate"; return 0; }
  # 2. Any explicitly-set valid NDK var (e.g. ANDROID_NDK_ROOT).
  local v
  for v in "${ANDROID_NDK:-}" "${ANDROID_NDK_HOME:-}" "${ANDROID_NDK_ROOT:-}"; do
    [ -n "$v" ] && [ -f "$v/source.properties" ] && { echo "$v"; return 0; }
  done
  # 3. Broader search for any installed NDK as a fallback.
  for base in "$sdk" "${ANDROID_HOME:-}" "/usr/local/lib/android/sdk" "$HOME/Android/Sdk"; do
    candidate="$(ls -d "$base"/ndk/*/ 2>/dev/null | head -1)"
    [ -n "$candidate" ] && [ -f "$candidate/source.properties" ] && { echo "$candidate"; return 0; }
  done
  echo "ERROR: cannot resolve Android NDK $ver (ANDROID_HOME=$ANDROID_HOME)" >&2
  exit 1
}
export ANDROID_NDK_HOME="$(resolve_ndk "$NDK_VERSION")"
export ANDROID_NDK="$ANDROID_NDK_HOME"
export ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"
# Fixed target dir so CI can cache the (slow) Rust build across runs.
export CARGO_TARGET_DIR="/tmp/alvr_target"

echo "=================================================="
echo " ALVR Qiyu Dream client builder"
echo " ALVR version : $ALVR_TAG"
echo " NDK home     : $ANDROID_NDK_HOME"
echo " Output dir   : $OUT_DIR"
echo "=================================================="

# ---- 1. Fetch ALVR source (shallow clone of the exact tag) ----
SRC="$(mktemp -d)"
cleanup() { rm -rf "$SRC"; }
trap cleanup EXIT
echo "==> Cloning ALVR $ALVR_TAG"
git clone --depth 1 --branch "$ALVR_TAG" "$ALVR_REPO" "$SRC/alvr"

# ALVR's `alvr_session` build.rs reads `openvr/headers/openvr_driver.h` (see
# alvr/session/build.rs). That file lives in the `openvr` git submodule, which
# a plain `git clone` does NOT fetch. Without it the build fails with:
#   "Missing openvr header files, did you clone the submodule?"
# Fetch submodules after clone. `--depth 1` keeps it fast (the openvr submodule
# is marked shallow in .gitmodules). Fall back to a full fetch if shallow fails.
echo "==> Fetching git submodules (openvr headers required by alvr_session)"
git -C "$SRC/alvr" submodule update --init --recursive --depth 1 \
  || git -C "$SRC/alvr" submodule update --init --recursive

# ---- 2. Toolchain (install only if missing; never swallows real errors) ----
echo "==> Rust target + tools"
rustup target add aarch64-linux-android >/dev/null 2>&1 || true
command -v cargo-ndk >/dev/null 2>&1 || cargo install cargo-ndk --version "$CARGO_NDK_VER"
command -v cbindgen  >/dev/null 2>&1 || cargo install cbindgen --version "$CBINDGEN_VER"

# ---- 3. Build alvr_client_core (aarch64, release) ----
# NOTE: cargo-ndk 4.x has NO `-p <platform>` short flag (it treats `-p` as a
# cargo package selector). The minimum Android API level is set via the
# CARGO_NDK_PLATFORM env var instead. `-p alvr_client_core` below is the cargo
# package selector (correct).
echo "==> Building alvr_client_core (slow step, ~10-40 min; cached across CI runs)"
cd "$SRC/alvr"
export CARGO_NDK_PLATFORM="$MIN_SDK"
cargo ndk -t "$NDK_TARGET" build -p alvr_client_core --release

SO_SRC="$CARGO_TARGET_DIR/aarch64-linux-android/release/libalvr_client_core.so"
[ -f "$SO_SRC" ] || { echo "ERROR: $SO_SRC not found" >&2; exit 1; }

# ---- 4. Generate the C header ----
mkdir -p "$OUT_DIR/include"
echo "==> Generating alvr_client_core.h (cbindgen)"
cbindgen "$SRC/alvr/alvr/client_core" \
  --config "$SRC/alvr/alvr/client_core/cbindgen.toml" \
  --lockfile "$SRC/alvr/Cargo.lock" \
  --output "$OUT_DIR/include/alvr_client_core.h"

# ---- 4b. DIAGNOSTIC: print the real generated ABI so failures are easy to debug ----
echo "=================================================================="
echo " GENERATED alvr_client_core.h — KEY ABI TOKENS (for debugging)"
echo "=================================================================="
grep -nE "ALVR_EVENT_|ALVR_BUTTON_VALUE_|ALVR_LOG_LEVEL_|enum AlvrEvent_Tag|AlvrEvent_Body|AlvrStreamConfig|AlvrStreamViewParams|AlvrLobbyViewParams|AlvrViewParams|AlvrDeviceMotion|AlvrFov|AlvrQuat|AlvrPose|void alvr_|bool alvr_|uint64_t alvr_|const char\* alvr_" \
  "$OUT_DIR/include/alvr_client_core.h" | head -90 || true
echo "=================================================================="

# ---- 5. Stage artifacts where the Gradle/CMake build expects them ----
mkdir -p "$OUT_DIR/$SO_ABI"
cp "$SO_SRC" "$OUT_DIR/$SO_ABI/libalvr_client_core.so"
cp "$OUT_DIR/include/alvr_client_core.h" "$OUT_DIR/alvr_client_core.h"

# ---- Bundle libc++_shared.so (REQUIRED by the prebuilt Qiyu SDK .so files) ----
# The Qiyu VR SDK prebuilts are dynamically linked against libc++_shared.so. If it
# isn't bundled, loading the Qiyu chain fails with "library ... not found". NDK
# ships it in a couple of locations depending on version/host, so try them all and
# WARN loudly if none is found (a silent miss is exactly what bit us before).
if [ -n "${ANDROID_NDK:-}" ]; then
  LIBCXX_CANDIDATES=(
    "$ANDROID_NDK/sources/cxx-stl/llvm-libc++/libs/$SO_ABI/libc++_shared.so"
    "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
    "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
    "$ANDROID_NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
  )
  LIBCXX=""
  for c in "${LIBCXX_CANDIDATES[@]}"; do
    if [ -f "$c" ]; then LIBCXX="$c"; break; fi
  done
  if [ -n "$LIBCXX" ]; then
    mkdir -p "$ROOT/app/src/main/jniLibs/$SO_ABI"
    cp "$LIBCXX" "$ROOT/app/src/main/jniLibs/$SO_ABI/libc++_shared.so"
    echo "==> Bundled libc++_shared.so (from $LIBCXX)"
  else
    echo "WARNING: libc++_shared.so NOT found in NDK -- Qiyu SDK native load will likely fail!" >&2
  fi
fi

echo "==> Artifacts staged:"
echo "   $OUT_DIR/$SO_ABI/libalvr_client_core.so"
echo "   $OUT_DIR/alvr_client_core.h"
echo ""
echo "==> Next: run 'gradle assembleRelease' (locally) or push to GitHub and let CI build the APK."
