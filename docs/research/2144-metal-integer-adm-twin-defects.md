<!-- markdownlint-disable MD013 -->
# Research-2144: `integer_adm_metal` on the first Apple report, six defects found on the host

State row: `T-METAL-INTEGER-ADM-TWIN-DEFECTS-2026-10-05`. Decision:
[ADR-1806](../adr/1806-metal-kernels-host-replay.md).

## What the report showed

The first M4 Pro report of the macOS tester bundle (issue #2118; macOS 26.6;
bundle built from `860050c3f`, which has the ADR-1498 port) failed all 15
exact cases of `test_metal_integer_adm_parity`, while
`test_adm_metal_registered` and `test_adm_rejects_below_min_dim` passed. The
parity gate (`--backends cpu metal --hold-exact metal --precision max`)
compares five outputs of `adm` (`integer_adm2` and the four scale ratios,
`scripts/ci/cross_backend_parity_gate.py`):

| Fixture | Frames | Max abs diff | Mismatches |
| --- | --- | --- | --- |
| Netflix 576x324, 8-bit | 48 | 2.8142593182341735 | 240 |
| 1080p checkerboard, 1 px | 3 | 0.49736109243464238 | 15 |
| 1080p checkerboard, 10 px | 3 | 0.52261743481103107 | 15 |
| Netflix 576x324, 10-bit | 3 | 2.771189095985048 | 15 |

Every output of every frame was wrong, by far more than a rounding
difference, while `float_adm_metal` was exact on the same fixtures. The
master commits after `860050c3f` that touch Metal (#2057, #2061, #2073) do not
touch the integer ADM kernel or host.

## Method

No Apple device is available. The shared decouple header
(`metal_integer_adm_math.h`) is held against the CPU by
`test_metal_integer_adm_math` and was right, so the defect had to be in the
rest of `integer_adm.metal` or in `integer_adm_metal.mm`. The kernel file
compiles unmodified as C++ with a 50-line shim for `<metal_stdlib>` (address
spaces and `kernel` removed, Metal types spelled, threadgroup atomics
serialised); the `.mm`'s submit and collect were transcribed with the same
buffer sizes and element types, and every threadgroup run as one thread. The
CPU reference was `integer_adm.c` itself (scalar path), compiled into the
same program. The replay of master's kernel gave the report's mismatch counts
on all four fixtures and the 10px cell's max abs diff to all 17 digits
(0.52261743481103107).

## The six defects

1. **Reduction slots at twice the stride.** Each stage-3 threadgroup stores
   nine 64-bit sums as uint32 pairs. The kernels computed
   `base = slot * 2u` with `slot = wg * 9` and then indexed
   `(base + band) * 2u`, a stride of 36 words; the host read a stride of 18.
   The host therefore summed band h for every row, band v for half the rows
   and band d not at all, and the kernels wrote past the end of each slot
   buffer (about 1400 words per frame at 576x324).
2. **Scale 1 read the int16 band as int32.** Scale 0 stores its bands as
   int16 (as `integer_adm.c`), and the CPU widens band a with `i16_to_i32()`
   before scale 1. The `.mm` bound that int16 buffer to
   `integer_adm_dwt_vert_s123`, which reads `const device int *`, so scales
   1-3 started from pairs of int16 samples read as one int32.
3. **+2^31 where the CPU adds INT32_MIN.** At scales 1-3 the CPU's 1/30
   neighbour term and 1/15 centre tap add `(int32_t)(1u << 31)`, which wraps
   to INT32_MIN (`i4_adm_round_terms()`, Netflix#955, ADR-0155), before the
   shift by 32. The kernel added `(long)(1u << 31)`, +2^31: every term one
   unit high.
4. **Denominator square rounding.** `i4_adm_csf_den_ctx_init()` adds
   `1u << shift_sq` (2^31, 2^30, 2^31) to each square; the `.mm` passed
   `1 << (shift_sq - 1)`.
5. **AIM at scale 0 under `adm_skip_scale0`.** The CPU returns from scale 0
   before the AIM stage; the `.mm` concluded a scale-0 AIM numerator anyway.
6. **Noise floor in float.** The `.mm`'s copies of the conclusion formed
   `(float)area * (float)noise_weight` where `adm_num_scale()` and
   `adm_den_scale_finalise()` form `area * adm_noise_weight` in double. Equal
   at the default 0.03125; at 0.02 (the default model) the argument differs
   on 27 % of areas between 1 and 2 million and `powf(x, 1/3)` on 9 %.

Each fixed on its own and put back on its own in the replay: defects 1, 2 and
3 each break all 27 case geometries of `test_metal_integer_adm_parity`,
defect 4 breaks 18, defect 5 the `adm_skip_scale0` case; defect 6 does not
show on the parity test's textured picture (the difference is lost in the
float sum) but does on a 256x144 crop of the Netflix pair and on noise,
ramp and sparse pictures at 256x144 under the model's options. With all six
fixed the replay equals the CPU on every case of the parity test (2160p and
962x13542 included), on 48 Netflix frames, both checkerboards and the 10-bit
pair, at gain limits 1.2 and 1.5, in Barten and both blend modes, and with
`adm_skip_scale0`.

## What is not explained

On the two Netflix pairs and the 1px checkerboard the replay of master's
kernel gives 0.707 / 0.682 / 0.464 where the device gave 2.814 / 2.771 /
0.497. The replay drops the writes past the slot buffers. Placing the four
slot buffers next to each other (so scale 0's overflow lands in scale 1's
slots) gives 1.13 / 1.09 / 2.63, which does not match either; where the
device's out-of-bounds writes landed is unknown. The fix removes them.

The report's `metal_equivalence` step exited 234 (`-EINVAL`) on both 1080p
checkerboards, with an `est_params` warning as the last line. Under the
default model's ADM options (`adm_csf_mode` 2, `adm_dlm_weight` 0.7,
`adm_enhn_gain_limit` 1, `adm_min_val` 0.5, `adm_noise_weight` 0.02,
`adm_p_norm` 2) every replayed frame of both checkerboards has finite,
non-zero numerators and denominators at every scale, with the overflow
dropped or landing in the next slot buffer (for example 23606 / 4387 on the
1px pair's first frame), so `collect()` of this twin returns no error there in
the replay. The exit is not attributed to `integer_adm_metal`.

## Where the host replay stops

One thread per threadgroup: the integer sums do not depend on the lane
split, but a missing barrier or a race between lanes is not seen. The Metal
compiler is not involved: the kernels' integer arithmetic is C++14's in both
languages, and fp32 code would only be as close as the strict FP flags make
it. The `.mm`'s Metal API calls are not run; the buffer bindings are written
twice (in the `.mm` and in the replay's runners), and only the sizes, element
types, plan, uniforms and conclusion are shared code.

## Reproducer

```bash
meson setup build-cpu core -Db_lto=false
ninja -C build-cpu test/test_metal_integer_adm_host_replay
VMAF_REPLAY_YUV_DIR=$PWD/python/test/resource/yuv \
  build-cpu/test/test_metal_integer_adm_host_replay
```
