#!/usr/bin/env bash
# Run a locally built release image the way the release smoke tests run the
# published one (docker-publish-production.yml, docker-publish-operator-node.yml).
#
# Usage: release-image-smoke.sh IMAGE KIND VERSION
#   IMAGE    a local image reference (nothing is pulled)
#   KIND     vmaf      the entrypoint is the `vmaf` CLI (production CLI image)
#            wrapped   the entrypoint is a Go binary that prints VERSION and the
#                      image carries /usr/local/bin/vmaf (vmafx-server)
#            node      wrapped, and the image carries ffmpeg and rclone (vmafx-node)
#            mcp       the vmaf-mcp server image: its Python, `vmaf-mcp --help` and
#                      /usr/local/bin/vmaf (the live HTTP startup stays a release
#                      smoke test)
#            binary    the entrypoint is a Go binary that prints VERSION and
#                      nothing else is checked (operator)
#   VERSION  what `--version` of a wrapped / node / binary entrypoint must print
#            (ignored for `vmaf` and `mcp`, whose version line is not the release's)
#
# `--version` never loads a model: the first v1.0.0-rc.1 images passed it although
# their builder lacked xxd and embedded no model. Every kind but `binary` is
# therefore also made to score two identical 576x324 frames without --model,
# which only works when the default model is compiled in.
#
# Exit status: 0 = every check passed, 1 = a check failed, 64 = bad usage.
#
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

set -euo pipefail

if [[ $# -ne 3 ]]; then
  printf 'Usage: release-image-smoke.sh IMAGE KIND VERSION\n' >&2
  exit 64
fi
image="$1"
kind="$2"
version="$3"
case "$kind" in
  vmaf | wrapped | node | mcp | binary) ;;
  *)
    printf 'ERROR: unknown KIND %s (vmaf, wrapped, node, mcp or binary)\n' "$kind" >&2
    exit 64
    ;;
esac

fail() {
  printf 'ERROR: %s\n' "$1" >&2
  exit 1
}

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT INT TERM

score_args=(--reference /frames/ref.yuv --distorted /frames/ref.yuv
  --width 576 --height 324 --pixel_format 420 --bitdepth 8
  --json --output /dev/stdout)

# run_score [DOCKER_RUN_ARGS...]: score the identical-frame pair through the image.
run_score() {
  local entry=("$@") frames="$scratch/frames" out
  mkdir -p "$frames"
  head -c $((576 * 324 * 3)) /dev/urandom >"$frames/ref.yuv"
  out="$(docker run --rm "${entry[@]}" -v "$frames:/frames:ro" "$image" "${score_args[@]}")" ||
    fail "$image: scoring two identical frames failed"
  jq -e '.pooled_metrics.vmaf.mean' <<<"$out" >/dev/null ||
    fail "$image: the score output has no pooled_metrics.vmaf.mean"
}

case "$kind" in
  vmaf)
    docker run --rm "$image" --version >/dev/null || fail "$image: --version failed"
    run_score
    ;;
  mcp)
    docker run --rm --entrypoint /venv/bin/python "$image" --version >/dev/null ||
      fail "$image: the image's Python does not start"
    docker run --rm --entrypoint /venv/bin/vmaf-mcp "$image" --help >/dev/null ||
      fail "$image: vmaf-mcp --help failed"
    run_score --entrypoint /usr/local/bin/vmaf
    ;;
  wrapped | node | binary)
    printed="$(docker run --rm "$image" --version)" || fail "$image: --version failed"
    [[ "$printed" == "$version" ]] ||
      fail "$image: --version printed '$printed', expected '$version'"
    if [[ "$kind" != binary ]]; then
      docker run --rm --entrypoint /usr/local/bin/vmaf "$image" --version >/dev/null ||
        fail "$image: /usr/local/bin/vmaf --version failed"
      run_score --entrypoint /usr/local/bin/vmaf
    fi
    if [[ "$kind" == node ]]; then
      docker run --rm --entrypoint /usr/local/bin/ffmpeg "$image" -version >/dev/null ||
        fail "$image: ffmpeg -version failed"
      docker run --rm --entrypoint /usr/local/bin/rclone "$image" version >/dev/null ||
        fail "$image: rclone version failed"
    fi
    ;;
esac
printf 'OK: %s (%s)\n' "$image" "$kind"
