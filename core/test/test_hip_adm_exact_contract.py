#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin the bit-exact adm_hip design (ADR-1423).

``adm_hip`` returns the CPU ``adm`` extractor's values bit for bit because
these things are the CPU's own and not a copy of them:

- the CSF weights: ``adm_csf_factors()`` of ``integer_adm_kernels.h``;
- the denominator fold: one rounding shift per row
  (``adm_csf_den_round_row_total()``, which the CPU's ``adm_csf_den_fold()``
  calls too), with the border and the shifts taken from the CPU's context
  initialisers. The kernels used to fold
  every thread's share of a row and to derive the shifts with the device
  ``log2f``, which is off by one for some region areas;
- the float conclusion of a scale: ``adm_cm_result()`` /
  ``i4_adm_cm_result()`` / ``adm_csf_den_result()`` /
  ``i4_adm_csf_den_result()``, including the seeds ``adm_skip_scale0`` leaves.

It also pins that a frame clears the result accumulators after its upload and
ahead of its kernels: queued ahead of the upload, the clear is lost in the
first context of a process that needs larger planes than the contexts before
it, and the frame adds onto whatever recycled device memory holds.

Device-free: reads the sources only. Every planted regression below is a
construct the pre-ADR-1423 twin had. ``test_hip_adm_exact`` checks the scores
on a device.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FEATURE_ROOT = ROOT / "core" / "src" / "feature"

HOST = "hip/integer_adm_hip.c"
DEN_KERNEL = "hip/integer_adm/adm_csf_den.hip"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
CPU_HEADER_INCLUDE = '#include "integer_adm_kernels.h"'
# A definition, not a call: `static ... name(` at the start of a line.
LOCAL_COPY = re.compile(
    r"^static\s+(?:inline\s+)?[\w\s\*]+?\b"
    r"(dwt_quant_step|adm_csf_factors|conclude_adm_cm|conclude_adm_csf_den)\s*\(",
    re.M,
)
CPU_RESULTS = (
    "adm_cm_result(&c, &bd, accum, s->adm_noise_weight, s->adm_p_norm)",
    "i4_adm_cm_result(&c, &bd, accum, s->adm_noise_weight, s->adm_p_norm)",
    "adm_csf_den_result(&c, accum, s->adm_noise_weight)",
    "i4_adm_csf_den_result(&c, accum, s->adm_noise_weight)",
)
# Each context initialiser feeds the launch and the result.
CPU_DEN_CONTEXTS = (
    "adm_csf_den_ctx_init(&c, w, h, s->adm_norm_view_dist",
    "i4_adm_csf_den_ctx_init(&c, scale, w, h, s->adm_norm_view_dist",
)
CONTEXT_USES = 2
SKIP_SCALE0_SEED = "float den_scale = (float)1e-10;"
DEVICE_LOG2 = re.compile(r"\blog2f?\s*\(")
ROW_FOLD = "(uint64_cu)adm_csf_den_round_row_total(row_total, add_shift_accum, shift_accum)"
ROW_LOOP = "for (int j = left + (int)threadIdx.x; j < right; j += ADM_CSF_DEN_THREADS)"
# One loop per kernel: scale 0, and scales 1 to 3.
ROW_LOOPS = 2
ROW_LAUNCHES = (
    "hipModuleLaunchKernel(s->func_adm_csf_den_scale_row_kernel, 1u, (uint32_t)rows,",
    "hipModuleLaunchKernel(s->func_adm_csf_den_s123_row_kernel, 1u, (uint32_t)rows,",
)
# A frame: upload, clear, kernels.
FRAME_UPLOAD = "adm_hip_stage_luma(s, frame, ref_pic, dis_pic)"
ACCUMULATOR_CLEAR = "hipMemsetAsync(buf->tmp_res, 0, sizeof(int64_t) * RES_BUFFER_SIZE, s->str)"
FIRST_KERNELS = "adm_hip_scale0(s, buf, ref_pic, dis_pic,"


def _code(source: str) -> str:
    """The source with its comments blanked, so prose cannot trip a check."""
    return COMMENT.sub(" ", source)


def _flat(source: str) -> str:
    """Comment-free source with every whitespace run collapsed to one space."""
    return re.sub(r"\s+", " ", _code(source))


def _calls(flat: str, call: str) -> int:
    """Occurrences of ``call`` that are not the tail of a longer identifier."""
    return len(re.findall(r"(?<![A-Za-z0-9_])" + re.escape(call), flat))


def _sources() -> dict[str, str]:
    return {name: (FEATURE_ROOT / name).read_text(encoding="utf-8") for name in (HOST, DEN_KERNEL)}


def _host_failures(sources: dict[str, str]) -> list[str]:
    failures: list[str] = []
    code = _code(sources[HOST])
    flat = _flat(sources[HOST])
    if CPU_HEADER_INCLUDE not in code:
        failures.append(f"{HOST}: the CPU's integer_adm_kernels.h is no longer included")
    for match in LOCAL_COPY.finditer(code):
        failures.append(f"{HOST}: local copy of {match.group(1)}() next to the CPU's routine")
    for call in CPU_RESULTS:
        if not _calls(flat, call):
            failures.append(f"{HOST}: a scale is no longer concluded by {call.split('(')[0]}()")
    for call in CPU_DEN_CONTEXTS:
        if _calls(flat, call) < CONTEXT_USES:
            failures.append(
                f"{HOST}: {call.split('(')[0]}() no longer feeds both the launch and the result"
            )
    if SKIP_SCALE0_SEED not in code:
        failures.append(
            f"{HOST}: adm_skip_scale0 no longer seeds the denominator with the CPU's float 1e-10"
        )
    for launch in ROW_LAUNCHES:
        if launch not in flat:
            failures.append(f"{HOST}: a denominator launch is no longer one block per row")
    return failures + _clear_failures(flat)


def _clear_failures(flat: str) -> list[str]:
    """The frame's accumulator clear lies between its upload and its kernels."""
    if _calls(flat, ACCUMULATOR_CLEAR) != 1:
        return [f"{HOST}: a frame no longer clears the result accumulators once"]
    clear = flat.find(ACCUMULATOR_CLEAR)
    upload = flat.rfind(FRAME_UPLOAD, 0, clear)
    kernels = flat.find(FIRST_KERNELS, clear)
    if upload < 0 or kernels < 0:
        return [
            f"{HOST}: the accumulator clear is no longer after the upload, ahead of the kernels"
        ]
    return []


def _kernel_failures(sources: dict[str, str]) -> list[str]:
    failures: list[str] = []
    code = _code(sources[DEN_KERNEL])
    flat = _flat(sources[DEN_KERNEL])
    if DEVICE_LOG2.search(code):
        failures.append(f"{DEN_KERNEL}: a rounding shift is derived on the device")
    if flat.count(ROW_FOLD) != 1:
        failures.append(f"{DEN_KERNEL}: the row total is no longer folded once, by the helper")
    if flat.count("atomicAdd(") != 1:
        failures.append(f"{DEN_KERNEL}: more than one accumulator update per row")
    fold = flat.find("atomicAdd(")
    guard = flat.rfind("if (threadIdx.x != 0) return;", 0, fold)
    sync = flat.rfind("__syncthreads();", 0, fold)
    if fold < 0 or guard < 0 or sync < 0 or sync > guard:
        failures.append(
            f"{DEN_KERNEL}: the fold is no longer thread 0's, after the block has been reduced"
        )
    if flat.count(ROW_LOOP) != ROW_LOOPS:
        failures.append(f"{DEN_KERNEL}: a block no longer covers a whole row of its band")
    return failures


def _contract_failures(sources: dict[str, str]) -> list[str]:
    return _host_failures(sources) + _kernel_failures(sources)


class AdmHipExactContract(unittest.TestCase):
    def assert_detected(self, sources: dict[str, str], needle: str) -> None:
        failures = _contract_failures(sources)
        self.assertTrue(any(needle in item for item in failures), failures)

    def test_sources_satisfy_the_contract(self) -> None:
        self.assertEqual(_contract_failures(_sources()), [])

    def test_local_csf_weight_copy_is_detected(self) -> None:
        sources = _sources()
        sources[HOST] += (
            "\nstatic inline float dwt_quant_step(const struct dwt_model_params *params,"
            " int lambda)\n{\n    return params->k * lambda;\n}\n"
        )
        self.assert_detected(sources, "local copy of dwt_quant_step")

    def test_missing_cpu_header_is_detected(self) -> None:
        sources = _sources()
        sources[HOST] = sources[HOST].replace(CPU_HEADER_INCLUDE, "", 1)
        self.assert_detected(sources, "no longer included")

    def test_own_conclusion_is_detected(self) -> None:
        # The pre-ADR-1423 conclude_adm_csf_den().
        sources = _sources()
        sources[HOST] = sources[HOST].replace(
            "return adm_csf_den_result(&c, accum, s->adm_noise_weight);",
            "return conclude_adm_csf_den(accum, h, w, scale);",
            1,
        )
        sources[HOST] += (
            "\nstatic float conclude_adm_csf_den(const uint64_t *accum, int h, int w, int scale)\n"
            "{\n    return (float)accum[0];\n}\n"
        )
        self.assert_detected(sources, "adm_csf_den_result()")
        self.assert_detected(sources, "local copy of conclude_adm_csf_den")

    def test_double_skip_scale0_seed_is_detected(self) -> None:
        sources = _sources()
        sources[HOST] = sources[HOST].replace(SKIP_SCALE0_SEED, "double den_scale = 1e-10;", 1)
        self.assert_detected(sources, "float 1e-10")

    def test_per_thread_fold_is_detected(self) -> None:
        # The pre-ADR-1423 kernels: every thread rounded its own partial sum.
        sources = _sources()
        sources[DEN_KERNEL] = sources[DEN_KERNEL].replace(
            "    thread_sums[threadIdx.x] = thread_sum;",
            "    atomicAdd(reinterpret_cast<uint64_cu *>(band_accum),\n"
            "              (thread_sum + add_shift_accum) >> shift_accum);\n"
            "    thread_sums[threadIdx.x] = thread_sum;",
            1,
        )
        self.assert_detected(sources, "more than one accumulator update")

    def test_fold_bypassing_the_helper_is_detected(self) -> None:
        sources = _sources()
        sources[DEN_KERNEL] = sources[DEN_KERNEL].replace(
            "adm_csf_den_round_row_total(row_total, add_shift_accum, shift_accum)",
            "((row_total + add_shift_accum) >> shift_accum)",
            1,
        )
        self.assert_detected(sources, "folded once, by the helper")

    def test_device_log2_shift_is_detected(self) -> None:
        sources = _sources()
        sources[DEN_KERNEL] = sources[DEN_KERNEL].replace(
            "uint64_cu thread_sum = 0;",
            "uint32_t shift = (uint32_t)ceilf(log2f((float)(right - left)));\n"
            "    uint64_cu thread_sum = shift;",
            1,
        )
        self.assert_detected(sources, "derived on the device")

    def test_block_per_row_chunk_launch_is_detected(self) -> None:
        # The pre-ADR-1423 launch: several blocks per row, each folding alone.
        sources = _sources()
        flat_launch = "hipModuleLaunchKernel(s->func_adm_csf_den_s123_row_kernel, 1u,"
        self.assertIn(flat_launch, sources[HOST])
        sources[HOST] = sources[HOST].replace(
            flat_launch,
            "hipModuleLaunchKernel(s->func_adm_csf_den_s123_row_kernel, blocks_per_row,",
            1,
        )
        self.assert_detected(sources, "one block per row")

    def test_uncleared_accumulators_are_detected(self) -> None:
        sources = _sources()
        self.assertIn(ACCUMULATOR_CLEAR, _flat(sources[HOST]))
        sources[HOST] = re.sub(
            r"hipMemsetAsync\(buf->tmp_res, 0,[^;]*;",
            "hipSuccess;",
            sources[HOST],
        )
        self.assert_detected(sources, "no longer clears the result accumulators once")

    def test_clear_ahead_of_the_upload_is_detected(self) -> None:
        # The pre-ADR-1423 frame: clear, upload, kernels.
        sources = _sources()
        clear = re.search(
            r"hipError_t hip_err = hipMemsetAsync\(buf->tmp_res, 0,[^;]*;", sources[HOST]
        )
        upload = "int err = adm_hip_stage_luma(s, frame, ref_pic, dis_pic);"
        self.assertIsNotNone(clear)
        self.assertIn(upload, sources[HOST])
        without = sources[HOST].replace(clear.group(0), "hipError_t hip_err = hipSuccess;", 1)
        sources[HOST] = without.replace(upload, clear.group(0) + "\n    " + upload, 1)
        self.assert_detected(sources, "no longer after the upload")


if __name__ == "__main__":
    unittest.main()
