#!/usr/bin/env bash
set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
src_dir="$repo_root/native/qt6"
build_dir="${BUILD_DIR:-$repo_root/build-linux-appimage}"
install_prefix="${INSTALL_PREFIX:-$build_dir/AppDir/usr}"
generator="${CMAKE_GENERATOR:-Ninja}"
build_type="${CMAKE_BUILD_TYPE:-Release}"

cmake_args=(
  -S "$src_dir"
  -B "$build_dir"
  -G "$generator"
  -DBUILD_APP=ON
  -DBUILD_TESTS=OFF
  -DCMAKE_INSTALL_LIBDIR=lib
)

if [[ "$generator" == "Ninja" ]]; then
  cmake_args+=("-DCMAKE_BUILD_TYPE=$build_type")
fi

if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
  cmake_args+=("-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH")
fi

if [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
  cmake_args+=("-DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE")
fi

if [[ -n "${VCPKG_TARGET_TRIPLET:-}" ]]; then
  cmake_args+=("-DVCPKG_TARGET_TRIPLET=$VCPKG_TARGET_TRIPLET")
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir" -j "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
cmake --install "$build_dir" --prefix "$install_prefix"

printf 'Built and installed AppDir staging at %s\n' "$install_prefix"
