#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
#
# zerocopy-throughput.sh - libvmaf_sycl zero-copy throughput harness (Phase 13).
#
# Runs a trimmed segment (or the whole file) of a real ref/dis pair through the
# QSV zero-copy path, R times per row, and records per run: fps (-benchmark
# rtime), the libvmaf_sycl timing line, host user/sys time, the DRM fdinfo engine
# counters of the ffmpeg process and the GPU frequency. scripts/test/
# zerocopy_throughput_report.py turns each run into one results.jsonl line and
# summarises the repeats with a spread gate.
#
# Runs inside the SYCL toolchain image on an Intel GPU:
#   SYCL_DEV_MEDIA_DIR=/data/downloads scripts/test/sycl-dev-container.sh exec \
#     bash scripts/test/zerocopy-throughput.sh --out DIR --ref PATH --dis PATH \
#     [--frames N] [--model NAME] [--feature SPEC] [--n-subsample K] [--repeat R]
#     [--env KEY=VAL]... [--label TAG] [--build-root DIR] [--ladder L0|L1|L2]
#     [--cpu-ref] [--vtune] [--dry-run] [--leg-timeout S]
#
# Attribution ladder: L0 decodes both inputs through the same trims into a null
# sink (no libvmaf); L1 runs libvmaf_sycl with no model and one feature; L2 runs
# the model. --dry-run prints each ffmpeg command as "CMD ..." and runs nothing.
# Segments use trim=end_frame=N on both inputs, never -frames:v.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPORT="$HERE/zerocopy_throughput_report.py"
# shellcheck source=/dev/null  # lib/qsv.sh (QSV_INIT)
source "$HERE/lib/qsv.sh"

OUT=""
REF=""
DIS=""
FRAMES=200
MODEL="vmaf_v0.6.1"
FEATURE=""
N_SUB=1
REPEAT=3
LABEL="run"
BUILD_ROOT="/work/.cache/sycl-dev"
LADDER="L2"
CPU_REF=0
VTUNE=0
DRY=0
LEG_TIMEOUT=900
ENV_ROWS=()
ENV_JOINED=""
BASES=()
VTUNE_BIN=/opt/intel/oneapi/vtune/latest/bin64/vtune
SCORE_FMT="%.17g"
CMD=()

usage() {
  cat >&2 <<'USAGE'
usage: zerocopy-throughput.sh --out DIR --ref PATH --dis PATH [--frames N]
         [--model NAME] [--feature SPEC] [--n-subsample K] [--repeat R]
         [--env KEY=VAL]... [--label TAG] [--build-root DIR]
         [--ladder L0|L1|L2] [--cpu-ref] [--vtune] [--dry-run] [--leg-timeout S]
USAGE
  exit 2
}

# set_opt FLAG VALUE: assign one value-taking option.
set_opt() {
  case "$1" in
    --out) OUT="$2" ;;
    --ref) REF="$2" ;;
    --dis) DIS="$2" ;;
    --frames) FRAMES="$2" ;;
    --model) MODEL="$2" ;;
    --feature) FEATURE="$2" ;;
    --n-subsample) N_SUB="$2" ;;
    --repeat) REPEAT="$2" ;;
    --label) LABEL="$2" ;;
    --build-root) BUILD_ROOT="$2" ;;
    --ladder) LADDER="$2" ;;
    --leg-timeout) LEG_TIMEOUT="$2" ;;
    *) usage ;;
  esac
}

parse_args() {
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --cpu-ref) CPU_REF=1 && shift ;;
      --vtune) VTUNE=1 && shift ;;
      --dry-run) DRY=1 && shift ;;
      --env) ENV_ROWS+=("${2:?}") && shift 2 ;;
      --*) set_opt "$1" "${2?}" && shift 2 ;;
      *) usage ;;
    esac
  done
}

validate_args() {
  [ -n "$OUT" ] && [ -n "$REF" ] && [ -n "$DIS" ] || usage
  case "$FRAMES" in '' | *[!0-9]*) usage ;; esac
  case "$N_SUB" in '' | *[!0-9]* | 0) usage ;; esac
  case "$REPEAT" in '' | *[!0-9]* | 0) usage ;; esac
  case "$LEG_TIMEOUT" in '' | *[!0-9]* | 0) usage ;; esac
  case "$LADDER" in L0 | L1 | L2) ;; *) usage ;; esac
  if [ "$LADDER" = L1 ] && [ -z "$FEATURE" ]; then FEATURE="name=motion"; fi
  local IFS=,
  ENV_JOINED="${ENV_ROWS[*]:-}"
}

# trim_chain INDEX LABEL: the per-input trim (null when the whole file is used).
trim_chain() {
  if [ "$FRAMES" = 0 ]; then
    printf '[%s:v]null[%s]' "$1" "$2"
  else
    printf '[%s:v]trim=end_frame=%s[%s]' "$1" "$FRAMES" "$2"
  fi
}

# filter_options BASE: the libvmaf option string of the ladder step.
filter_options() {
  local opt
  if [ "$LADDER" = L1 ] || [ -z "$MODEL" ]; then
    opt="model="
  else
    opt="model=version=${MODEL}"
  fi
  if [ -n "$FEATURE" ]; then opt="${opt}:feature='${FEATURE}'"; fi
  printf '%s:n_subsample=%s:score_fmt=%s:log_fmt=json:log_path=%s.json' \
    "$opt" "$N_SUB" "$SCORE_FMT" "$1"
}

# qsv_inputs: the two hardware-decoded inputs, one QSV device each.
qsv_inputs() {
  printf '%s\n' -hwaccel qsv -hwaccel_output_format qsv -hwaccel_device qd -i "$DIS" \
    -hwaccel qsv -hwaccel_output_format qsv -hwaccel_device qr -i "$REF"
}

# build_cmd BASE KIND: fill CMD with the ffmpeg command (KIND: zc or cpu).
build_cmd() {
  local base="$1" kind="$2" d r dl rl
  CMD=(ffmpeg -hide_banner -nostats -y -benchmark "${QSV_INIT[@]}")
  mapfile -t -O "${#CMD[@]}" CMD < <(qsv_inputs)
  d="$(trim_chain 0 d)"
  r="$(trim_chain 1 r)"
  if [ "$kind" = cpu ]; then
    dl="${d/\[d\]/,hwdownload,format=p010le[d]}"
    rl="${r/\[r\]/,hwdownload,format=p010le[r]}"
    CMD+=(-lavfi "$dl;$rl;[d][r]libvmaf=$(filter_options "$base")" -f null -)
  elif [ "$LADDER" = L0 ]; then
    CMD+=(-filter_complex "$d;$r" -map "[d]" -f null - -map "[r]" -f null -)
  else
    CMD+=(-lavfi "$d;$r;[d][r]libvmaf_sycl=$(filter_options "$base")" -f null -)
  fi
}

check_build_root() {
  local ff lib
  ff="$(command -v ffmpeg)"
  lib="$(ldd "$ff" | awk '/libvmaf/ {print $3; exit}')"
  case "$lib" in
    "$BUILD_ROOT"/*) ;;
    *)
      echo "BUILD-ROOT MISMATCH: libvmaf resolves to '${lib:-none}', expected under $BUILD_ROOT" >&2
      exit 1
      ;;
  esac
}

write_provenance() {
  {
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "build_root: $BUILD_ROOT"
    echo "libvmaf: $(ldd "$(command -v ffmpeg)" | awk '/libvmaf/ {print $3; exit}')"
    echo "kernel: $(uname -r)"
    if [ -f "$BUILD_ROOT/COMMIT" ]; then echo "commit: $(cat "$BUILD_ROOT/COMMIT")"; fi
    echo "env rows: ${ENV_ROWS[*]:-}"
    echo "packages:"
    if dpkg-query -W libze-intel-gpu1 intel-oneapi-runtime-dpcpp-cpp 2>/dev/null; then :; fi
  } >"$OUT/provenance.txt"
}

# render_pid: PID of the newest process named ffmpeg (empty when none).
render_pid() {
  local d
  for d in /proc/[0-9]*; do
    if [ "$(cat "$d/comm" 2>/dev/null)" = ffmpeg ]; then
      printf '%s\n' "${d#/proc/}"
      return 0
    fi
  done
  return 1
}

# snap_fdinfo PID DEST: every render-node fdinfo of PID, stamped with the epoch.
snap_fdinfo() {
  local pid="$1" dest="$2" fd found=1
  {
    echo "t: $(date +%s.%N)"
    for fd in /proc/"$pid"/fd/*; do
      if [ "$(readlink "$fd" 2>/dev/null)" = /dev/dri/renderD128 ]; then
        echo "--"
        if cat "/proc/$pid/fdinfo/${fd##*/}" 2>/dev/null; then found=0; fi
      fi
    done
  } >"$dest.tmp"
  if [ "$found" = 0 ]; then mv "$dest.tmp" "$dest"; else rm -f "$dest.tmp"; fi
}

snap_freq() {
  local f
  for f in /sys/class/drm/card*/gt_act_freq_mhz /sys/class/drm/card*/gt/gt0/rps_act_freq_mhz; do
    if [ -r "$f" ]; then
      cat "$f" >>"$1.freq"
      return 0
    fi
  done
}

# sample_run BASE RUNNER_PID: bounded sampler (HISS-02) until the runner exits.
sample_run() {
  local base="$1" runner="$2" i pid max=$((LEG_TIMEOUT * 2))
  for ((i = 0; i < max; i++)); do
    if ! kill -0 "$runner" 2>/dev/null; then break; fi
    if pid="$(render_pid)"; then
      if [ ! -f "$base.fdinfo.first" ]; then snap_fdinfo "$pid" "$base.fdinfo.first"; fi
      snap_fdinfo "$pid" "$base.fdinfo.last"
      snap_freq "$base"
    fi
    sleep 0.5
  done
}

# run_one BASE COMMAND...: run the command, record time, rc and samples.
run_one() {
  local base="$1" runner
  shift
  rm -f "$base".{err,out,rc,time,fdinfo.first,fdinfo.last,freq,json}
  TIMEFORMAT='TIME real=%R user=%U sys=%S'
  {
    time {
      rc=0
      timeout "$LEG_TIMEOUT" "$@" >"$base.out" 2>"$base.err" || rc=$?
      echo "$rc" >"$base.rc"
    }
  } 2>"$base.time" &
  runner=$!
  sample_run "$base" "$runner"
  wait "$runner"
}

record_run() {
  local base="$1" rep="$2" slug="$3"
  python3 "$REPORT" parse-run --base "$base" --meta "label=$LABEL" "ladder=$LADDER" \
    "model=$MODEL" "feature=$FEATURE" "frames=$FRAMES" "n_subsample=$N_SUB" \
    "env=$ENV_JOINED" "build_root=$BUILD_ROOT" "repeat=$rep" "slug=$slug"
}

# do_leg BASE KIND REPEAT_INDEX: one run (or the dry-run command line).
do_leg() {
  local base="$1" kind="$2" rep="$3" wrap=()
  build_cmd "$base" "$kind"
  BASES+=("$base")
  if [ "$DRY" = 1 ]; then
    echo "CMD ${CMD[*]}"
    return 0
  fi
  if [ "$VTUNE" = 1 ] && [ "$rep" = 1 ] && [ "$kind" = zc ]; then
    wrap=("$VTUNE_BIN" -collect xpu-offload -r "$OUT/vtune-$LABEL" --)
  fi
  run_one "$base" "${wrap[@]}" env "${ENV_ROWS[@]}" "${CMD[@]}"
  record_run "$base" "$rep" "$kind"
}

main() {
  parse_args "$@"
  validate_args
  OUT="$(realpath -m "$OUT")"
  mkdir -p "$OUT"
  if [ "$DRY" = 0 ]; then
    export PATH="$BUILD_ROOT/ffmpeg-prefix/bin:$BUILD_ROOT/prefix/bin:$PATH"
    export LD_LIBRARY_PATH="$BUILD_ROOT/prefix/lib/x86_64-linux-gnu:$BUILD_ROOT/prefix/lib:${LD_LIBRARY_PATH:-}"
    check_build_root
    if [ "$VTUNE" = 1 ] && [ ! -x "$VTUNE_BIN" ]; then
      echo "VTUNE UNAVAILABLE: $VTUNE_BIN" >&2
      exit 1
    fi
    write_provenance
  fi
  local slug="${FEATURE:-$MODEL}" base i
  slug="${slug//[^A-Za-z0-9._-]/_}"
  for ((i = 1; i <= REPEAT; i++)); do
    base="$OUT/$LABEL-$LADDER-${slug:-nomodel}-n$N_SUB-r$i"
    do_leg "$base" zc "$i"
  done
  if [ "$CPU_REF" = 1 ]; then do_leg "$OUT/$LABEL-$LADDER-${slug:-nomodel}-n$N_SUB-cpu" cpu 1; fi
  if [ "$DRY" = 1 ]; then return 0; fi
  local f failed=0
  for f in "${BASES[@]}"; do
    if [ "$(cat "$f.rc" 2>/dev/null)" != 0 ]; then failed=1; fi
  done
  if [ "$failed" = 1 ]; then
    echo "RUN FAILED: see $OUT/*.rc" >&2
    exit 1
  fi
}

main "$@"
