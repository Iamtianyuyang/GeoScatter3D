#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
native_build="$repo_root/tmp/build-linux"

cmake -S "$repo_root" -B "$native_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$native_build" --target gs3d_ffi gs3d_pipeline_ffi gs3d_shaders --parallel "${GS3D_BUILD_JOBS:-4}"

cd "$repo_root/ui_flutter"
flutter build linux --release
