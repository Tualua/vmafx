#!/usr/bin/env bash
# test_cli.sh — smoke-test the `vmaf --tiny-model` option.
#
# Requires: meson build with -Denable_dnn=enabled, an ONNX model under
# model/tiny/ (any), and libonnxruntime on the runtime library path.
#
# When DNN is disabled, asserts the clear error message instead.
set -eu

: "${VMAF_BIN:=build/tools/vmaf}"

if [[ ! -x "$VMAF_BIN" ]]; then
  echo "vmaf binary not found at $VMAF_BIN — set VMAF_BIN=<path>" >&2
  exit 77 # meson's "skipped"
fi

# `vmaf --help` exits with 1 by convention, so capture the output first
# instead of piping into grep under set -o pipefail.
help_text="$("$VMAF_BIN" --help 2>&1 || true)"

# 1. Help text must advertise the tiny flags.
printf '%s\n' "$help_text" | grep -q -- '--tiny-model' || {
  echo "help missing --tiny-model"
  exit 1
}
printf '%s\n' "$help_text" | grep -q -- '--tiny-device' || {
  echo "help missing --tiny-device"
  exit 1
}
printf '%s\n' "$help_text" | grep -q -- '--no-reference' || {
  echo "help missing --no-reference"
  exit 1
}

# 2. Invalid device string must be rejected with a useful message.
# The keyword list grew with coreml / coreml-{ane,gpu,cpu} (ADR-0365)
# and openvino-{npu,cpu,gpu} (Research-0031 / A.5);
# match the stable head + tail rather than the verbatim middle so this
# stays passing across future grammar additions.
if "$VMAF_BIN" --tiny-model /nonexistent.onnx --tiny-device bogus 2>&1 |
  grep -qiE 'auto\|cpu\|cuda\|openvino.*rocm'; then
  :
else
  echo "expected validation error for --tiny-device bogus"
  exit 1
fi

# 3. The new coreml / coreml-{ane,gpu,cpu} keywords must be accepted by
# the validator. `vmaf` exits non-zero because we don't supply a
# reference YUV, but the rejection message would mention "Invalid
# argument" if the keyword itself were unknown. We only assert the
# keyword does not surface as a validation error.
for dev in coreml coreml-ane coreml-gpu coreml-cpu; do
  out="$("$VMAF_BIN" --tiny-device "$dev" 2>&1 || true)"
  if printf '%s\n' "$out" | grep -q "Invalid argument \"$dev\""; then
    echo "validator wrongly rejected --tiny-device $dev"
    exit 1
  fi
done

# 4. The new openvino-{npu,cpu,gpu} keywords must be accepted by the
# validator. `vmaf` exits non-zero because we don't supply a reference
# YUV, but the rejection message would mention "Invalid argument" if the
# keyword itself were unknown. We only assert the keyword does not
# surface as a validation error.
for dev in openvino-npu openvino-cpu openvino-gpu; do
  out="$("$VMAF_BIN" --tiny-device "$dev" 2>&1 || true)"
  if printf '%s\n' "$out" | grep -q "Invalid argument \"$dev\""; then
    echo "validator wrongly rejected --tiny-device $dev"
    exit 1
  fi
done

# 5a. --no-reference without --tiny-model is rejected with the specific
# diagnostic from ADR-0519. Before that ADR the flag was a documented
# no-op: the parser set CLISettings::no_reference but the gate downstream
# always required a reference path. Asserts both the new error message
# AND the absence of the legacy "Reference .y4m or .yuv (-r/--reference)
# is required" message that would surface if the gate were still
# unconditional.
nr_noref_out="$("$VMAF_BIN" --no-reference --distorted /dev/null \
  --width 64 --height 64 --pixel_format 420 --bitdepth 8 2>&1 || true)"
if ! printf '%s\n' "$nr_noref_out" | grep -q -- '--no-reference requires --tiny-model'; then
  printf '%s\n' "$nr_noref_out"
  echo "ADR-0520: --no-reference without --tiny-model did not emit the expected diagnostic"
  exit 1
fi
if printf '%s\n' "$nr_noref_out" | grep -q 'Reference .y4m or .yuv'; then
  printf '%s\n' "$nr_noref_out"
  echo "ADR-0520: --no-reference still trips the legacy reference-required gate"
  exit 1
fi

# 5b. --no-reference --tiny-model X --distorted Y must clear the
# reference-required gate. We use dists_sq.onnx (a 2-input FR placeholder
# that load-fails) because the shipped NR-marked models all use a
# symbolic batch dim that the loader rejects (a separate pre-existing
# limitation, tracked outside ADR-0519). The point of this assertion is
# the *absence* of the legacy reference-required diagnostic — once that
# fires the CLI exits before model load, and the test would never see
# the model-loader's error. A "problem loading tiny model" message is
# the success indicator: it proves the CLI advanced past the gate.
DIST_YUV="python/test/resource/yuv/src01_hrc01_576x324.yuv"
if [[ -f "$DIST_YUV" && -f model/tiny/dists_sq.onnx ]]; then
  nr_dist_out="$("$VMAF_BIN" --no-reference --tiny-model model/tiny/dists_sq.onnx \
    --tiny-device cpu --distorted "$DIST_YUV" \
    --width 576 --height 324 --pixel_format 420 --bitdepth 8 2>&1 || true)"
  if printf '%s\n' "$nr_dist_out" | grep -q 'Reference .y4m or .yuv'; then
    printf '%s\n' "$nr_dist_out"
    echo "ADR-0520: --no-reference --tiny-model still trips the reference-required gate"
    exit 1
  fi
  if ! printf '%s\n' "$nr_dist_out" | grep -qE 'problem loading tiny model|vmaf_tiny_model|frame'; then
    printf '%s\n' "$nr_dist_out"
    echo "ADR-0520: --no-reference --tiny-model did not reach the loader / frame loop"
    exit 1
  fi
else
  echo "ADR-0519: dist YUV or dists_sq model missing; skipping NR end-to-end smoke"
fi

# 6. Feature-vector + external-data ONNX models load successfully
# (ADR-0518). Each of the shipped tiny FR-regressor checkpoints carries
# either rank-2 inputs (feature vector), external-data weights, or both.
# Before ADR-0517 the C-side loader rejected all three with -ENOTSUP
# (errno 95 / EOPNOTSUPP). This block runs the full load + per-frame
# inference pipeline against the Netflix CPU reference YUVs and asserts
# (a) no `-95` load error surfaces and (b) the `vmaf_tiny_model` feature
# column appears in the JSON output. The exact score is intentionally
# not pinned — that is the job of the regen-snapshots gate. The smoke
# gate cares about load + run, not numerical drift.
#
# Requires the source-tree YUV fixtures + the shipped tiny models. The
# meson harness sets cwd to the project root so the relative paths
# below resolve against the source tree, not the build dir.
SRC_YUV="python/test/resource/yuv/src01_hrc00_576x324.yuv"
DST_YUV="python/test/resource/yuv/src01_hrc01_576x324.yuv"
if [[ -f "$SRC_YUV" && -f "$DST_YUV" ]]; then
  for M in \
    model/tiny/fr_regressor_v1.onnx \
    model/tiny/fr_regressor_v2.onnx \
    model/tiny/vmaf_tiny_v4.onnx; do
    if [[ ! -f "$M" ]]; then
      echo "missing tiny model: $M (skipping that case)"
      continue
    fi
    json_out="$(mktemp -t vmaf_tiny_smoke_XXXXXX.json)"
    if ! out="$("$VMAF_BIN" \
      --reference "$SRC_YUV" --distorted "$DST_YUV" \
      --width 576 --height 324 --pixel_format 420 --bitdepth 8 \
      --tiny-model "$M" --tiny-device cpu \
      --json --output "$json_out" 2>&1)"; then
      echo "$out"
      echo "tiny-model smoke load FAILED for $M"
      rm -f "$json_out"
      exit 1
    fi
    if printf '%s\n' "$out" | grep -q 'problem loading tiny model'; then
      printf '%s\n' "$out"
      echo "tiny-model smoke load surfaced load-error for $M"
      rm -f "$json_out"
      exit 1
    fi
    if ! grep -q 'vmaf_tiny_model' "$json_out"; then
      echo "tiny-model smoke: vmaf_tiny_model missing from JSON for $M"
      rm -f "$json_out"
      exit 1
    fi
    rm -f "$json_out"
  done
else
  echo "Netflix YUV fixtures not present at $SRC_YUV; skipping feature-vector smoke"
fi

echo "PASS: $0"
