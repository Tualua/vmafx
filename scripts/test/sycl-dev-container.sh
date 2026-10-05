#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
#
# sycl-dev-container.sh - build libvmaf and ffmpeg from this worktree inside the
# SYCL toolchain image and run tests / commands on the Intel GPU.
#
# Usage:
#   scripts/test/sycl-dev-container.sh libvmaf          # JIT libvmaf build + install
#   scripts/test/sycl-dev-container.sh ffmpeg           # ffmpeg + this tree's patch series
#   scripts/test/sycl-dev-container.sh test [args...]   # meson test in the cache build dir
#   scripts/test/sycl-dev-container.sh exec <cmd...>    # run a command with the built ffmpeg/libvmaf
#
# Environment:
#   CONTAINER_RT       container CLI (default: podman, then docker)
#   SYCL_DEV_IMAGE     toolchain image (default: localhost/vmafx:build-ocloc)
#   SYCL_DEV_CC        C compiler for the libvmaf build (default: icx)
#   SYCL_DEV_TIMEOUT   per-run wall-clock cap in seconds (default: 5400)
#   YUV_DIR            fixture directory mounted read-only at /yuv
#   HOST_REPO          host path of the main checkout when running in a devcontainer
#                      (the runtime is the host's, so -v sources must be host paths)

set -euo pipefail

usage() {
  cat >&2 <<'EOF'
usage: sycl-dev-container.sh {libvmaf|ffmpeg|test [args...]|exec <cmd...>}
EOF
  exit 2
}

[ "$#" -ge 1 ] || usage
MODE="$1"
shift
case "$MODE" in
  libvmaf | ffmpeg | test | exec) ;;
  *) usage ;;
esac
if [ "$MODE" = exec ] && [ "$#" -lt 1 ]; then
  usage
fi

RT="${CONTAINER_RT:-$(command -v podman || command -v docker || true)}"
if [ -z "$RT" ]; then
  echo "error: no container runtime (podman/docker) on PATH." >&2
  exit 127
fi

IMAGE="${SYCL_DEV_IMAGE:-localhost/vmafx:build-ocloc}"
TIMEOUT="${SYCL_DEV_TIMEOUT:-5400}"
WORKTREE="$(git rev-parse --show-toplevel)"
COMMON="$(git rev-parse --path-format=absolute --git-common-dir)"
MAIN_ROOT="$(dirname "$COMMON")"
YUV_DIR="${YUV_DIR:-$MAIN_ROOT/python/test/resource/yuv}"

# Rewrite a path under MAIN_ROOT to the same suffix under HOST_REPO.
host_path() {
  local hrepo="${HOST_REPO:-$MAIN_ROOT}"
  case "$1" in
    "$MAIN_ROOT") printf '%s\n' "$hrepo" ;;
    "$MAIN_ROOT"/*) printf '%s\n' "$hrepo${1#"$MAIN_ROOT"}" ;;
    *) printf '%s\n' "$1" ;;
  esac
}

# Script executed inside the container. Quoted heredoc: no host expansion.
INNER="$(
  cat <<'EOS'
set -euo pipefail
MODE="$1"
shift
C=/work/.cache/sycl-dev
mkdir -p "$C"
PC="$C/prefix/lib/x86_64-linux-gnu/pkgconfig:$C/prefix/lib/pkgconfig"

build_libvmaf() {
  if [ ! -f "$C/build/build.ninja" ]; then
    CC="${SYCL_DEV_CC:-icx}" CXX=icpx CC_LD=lld CXX_LD=lld meson setup "$C/build" core \
      -Denable_cuda=false -Denable_sycl=true -Denable_dnn=disabled \
      --buildtype=release -Db_lto=false -Dsycl_icpx_aot_targets= \
      --prefix="$C/prefix"
  fi
  ninja -C "$C/build"
  ninja -C "$C/build" install
  mkdir -p "$C/prefix/include/libvmaf"
  find core "$C/build" -path '*include/libvmaf/*.h' -exec cp -f {} "$C/prefix/include/libvmaf/" \;
}

build_ffmpeg() {
  local tag n
  if [ ! -d "$C/ffmpeg-src" ]; then
    cp -a /src/ffmpeg-src "$C/ffmpeg-src"
  fi
  tag="$(sed -n 's/^FFMPEG_TAG="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' /work/build-config.env | head -n1)"
  cd "$C/ffmpeg-src"
  if [ -n "$tag" ] && git rev-parse --verify -q "$tag^{commit}" >/dev/null; then
    git reset -q --hard "$tag"
  else
    n="$(grep -cvE '^[[:space:]]*(#|$)' /src/vmafx/ffmpeg-patches/series.txt)"
    git reset -q --hard "HEAD~$n"
  fi
  git config user.email sycl-dev@example.invalid
  git config user.name sycl-dev
  local p
  while read -r p; do
    case "$p" in '' | '#'*) continue ;; esac
    if ! git am --3way "/work/ffmpeg-patches/$p" >/dev/null 2>&1; then
      git am --abort >/dev/null 2>&1 || true
      echo "PATCH FAILED: $p" >&2
      exit 1
    fi
  done </work/ffmpeg-patches/series.txt
  PKG_CONFIG_PATH="$PC${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" ./configure \
    --prefix="$C/ffmpeg-prefix" --enable-gpl --enable-version3 --enable-libvmaf \
    --enable-libdav1d --enable-vaapi --enable-libvpl --enable-libvmaf-sycl \
    --disable-filter=vmaf_pre --disable-doc --disable-debug
  make -j"$(nproc)" install
  PATH="$C/ffmpeg-prefix/bin:$PATH" LD_LIBRARY_PATH="$C/prefix/lib/x86_64-linux-gnu:$C/prefix/lib:${LD_LIBRARY_PATH:-}" \
    ffmpeg -hide_banner -filters | grep libvmaf
}

case "$MODE" in
  libvmaf) build_libvmaf ;;
  ffmpeg) build_ffmpeg ;;
  test) python3 /work/scripts/ci/run_meson_test.py -- -C "$C/build" --print-errorlogs "$@" ;;
  exec)
    export PATH="$C/ffmpeg-prefix/bin:$C/prefix/bin:$PATH"
    export LD_LIBRARY_PATH="$C/prefix/lib/x86_64-linux-gnu:$C/prefix/lib:${LD_LIBRARY_PATH:-}"
    "$@"
    ;;
esac
EOS
)"

exec timeout --signal=KILL "$TIMEOUT" "$RT" run --rm \
  --device /dev/dri --security-opt label=disable \
  -e UR_L0_USE_IMMEDIATE_COMMANDLISTS=0 \
  -e SYCL_DEV_CC="${SYCL_DEV_CC:-icx}" \
  -v "$(host_path "$WORKTREE"):/work" \
  -v "$(host_path "$YUV_DIR"):/yuv:ro" \
  -w /work --entrypoint bash "$IMAGE" -c "$INNER" _ "$MODE" "$@"
