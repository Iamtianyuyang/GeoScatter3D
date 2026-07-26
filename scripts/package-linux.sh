#!/usr/bin/env bash
# One-click Linux build + package.
#
# Builds Release, runs the test suite, then produces the self-contained
# distribution folder (dist/) and the CPack TGZ.
#
# Usage:
#   ./scripts/package-linux.sh                 # full pipeline
#   ./scripts/package-linux.sh --skip-tests    # skip ctest
#   BUILD_DIR=out ./scripts/package-linux.sh   # custom build directory

set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT=$(pwd)
BUILD_DIR=${BUILD_DIR:-build-linux}

SKIP_TESTS=0
for arg in "$@"; do
    case "$arg" in
        --skip-tests) SKIP_TESTS=1 ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

# --- Dependency checks ------------------------------------------------------
missing=()
command -v cmake            >/dev/null || missing+=(cmake)
command -v python3          >/dev/null || missing+=(python3)
command -v glslangValidator >/dev/null || missing+=(glslang-tools)
pkg-config --exists glfw3 2>/dev/null  || missing+=(libglfw3-dev)
pkg-config --exists vulkan 2>/dev/null || missing+=(libvulkan-dev)

if [ ${#missing[@]} -gt 0 ]; then
    echo "Missing dependencies: ${missing[*]}" >&2
    echo "On Debian/Ubuntu:" >&2
    echo "  sudo apt-get install ninja-build libglfw3-dev libvulkan-dev glslang-tools" >&2
    exit 1
fi

echo "==> Initializing git submodules"
git submodule update --init --recursive

# --- Configure + build ------------------------------------------------------
GENERATOR=()
command -v ninja >/dev/null && GENERATOR=(-G Ninja)

# BUILD_TESTING is passed explicitly so a stale cache with tests disabled
# can never silently skip the test targets.
echo "==> Configuring ($BUILD_DIR)"
cmake -B "$BUILD_DIR" -S . "${GENERATOR[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON

echo "==> Building Release"
cmake --build "$BUILD_DIR" --parallel

if [ "$SKIP_TESTS" -eq 0 ]; then
    echo "==> Running tests"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

# --- Install + package ------------------------------------------------------
echo "==> Installing to dist/"
rm -rf dist
cmake --install "$BUILD_DIR" --prefix dist

echo "==> Creating package"
(cd "$BUILD_DIR" && cpack)

PACKAGE=$(ls -t "$BUILD_DIR"/GeoScatter3D-*.tar.gz | head -1)
echo
echo "Done."
echo "  Folder:  $REPO_ROOT/dist/"
echo "  Archive: $REPO_ROOT/$PACKAGE"
echo
echo "Note: the Linux package links system glfw/vulkan; target machines"
echo "need libglfw3 and libvulkan1 installed."
