<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1491: The CUDA, SYCL and HIP motion twins compute `motion_five_frame_window`: the frame two back on the device, the CPU's window function on the host

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `motion`, `cuda`, `sycl`, `hip`, `gpu-parity`, `exact-twins`, `upstream-port`, `rc3`

## Context

[ADR-1478](1478-motion-five-frame-window-port.md) ported Netflix's
`motion_five_frame_window` to the CPU extractors `motion` and `motion_v2` and
left the GPU twins as they were: `motion_cuda`, `motion_sycl` and
`motion_hip` declared the option default-only, the `motion_v2` twins did not
declare it, and model dispatch computed the motion feature on the CPU
whenever the option was set (`T-GPU-MOTION-FIVE-FRAME-WINDOW-2026-10-02`).
The maintainer's decision was to port the option on the CPU and on the
twins, each twin returning the CPU's bits.

The twins of both extractors are declared exact
(`scripts/ci/exact_twins.d/`). What the option changes is small on the
device and larger on the host:

- **Device.** The SAD kernel already takes the two planes it differences as
  arguments. With the option one of them is the frame two back instead of
  the previous one, so the twin has to keep one more frame.
- **Host.** `motion2` of frame `n` becomes `min(SAD[n-1], SAD[n+1])`, with
  special cases at both ends, and frames 0 and 1 have no SAD. The twins of
  `motion` emit `motion2` and `motion3` frame by frame as SADs arrive, with
  their own copy of the three-frame arithmetic; the twins of `motion_v2`
  carry one copy each of the CPU's flush.

## Decision

Each twin takes its SAD against the frame two back when the option is set,
and derives `motion2` and `motion3` with the CPU's own function,
`vmaf_motion_window_flush()` (`core/src/feature/motion_window.h`,
ADR-1478), in `flush()`.

1. **The frame two back.** CUDA: the ring of raw planes has three slots
   instead of two; frame `n` writes slot `n % ring` and reads slot
   `(n + 1) % ring`. HIP: two kept planes instead of one; frame `n` reads
   plane `n % depth` and the copy behind the SAD overwrites it. SYCL
   `motion_v2_sycl`: a ring of three, as on CUDA. SYCL `motion_sycl`: its
   two planes get fixed roles (frame `n-2` and frame `n-1`) and two device
   copies after each frame's kernel advance them. No kernel changes on any
   backend.
2. **The first two frames** report a SAD of 0 from `collect()` and launch no
   readback. `motion_sycl` enqueues the kernel on every frame all the same
   and ignores the result of the first two: its work is recorded once into a
   command graph and replayed, so what a frame enqueues cannot depend on its
   index.
3. **The window on the host.** With the option, the twins of `motion` store
   the SAD score per frame and nothing else; `flush()` calls
   `vmaf_motion_window_flush()`. Their three-frame path is unchanged. The
   twins of `motion_v2` call the function for both windows, which removes
   their copies of the flush.
4. **Option tables.** `motion_cuda`, `motion_sycl` and `motion_hip` drop
   `VMAF_OPT_FLAG_DEFAULT_ONLY` and the `-ENOTSUP`; the three `motion_v2`
   twins declare the option as the CPU does.
5. **Gate.** Two cells, `motion_mffw` and `motion_v2_mffw` (the option with
   the moving average, the option set of the HFR models), exact for all
   three backends.
6. **Metal.** The Metal twins are not changed: they do not declare the
   option, so the CPU extractor keeps computing it there. Not run: no
   device.
7. `motion_sycl` refuses the option together with its own `motion_add_uv`
   (`-ENOTSUP`): the CPU `motion` has no chroma mode, so that combination has
   no reference to equal.

Statements of earlier ADRs this replaces: ADR-1478 decision 5 for the CUDA,
SYCL and HIP twins (they no longer leave the option to the CPU; the Metal
part stands), and ADR-0219's `-ENOTSUP` for `motion_five_frame_window` on
those three twins.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep the CPU fallback (the state after ADR-1478) | No twin code | The motion feature of the HFR models leaves the device on every GPU run; the maintainer asked for the twins | Not what was decided |
| Extend each `motion` twin's frame-by-frame emission to the five-frame window | `motion2` / `motion3` stay available before the flush, as in the three-frame path | A third copy of the window per backend, with the end cases (`SAD[3]` for frame 2, the last frame, one- to three-frame inputs) to get right three times; the CPU itself has the scores only at the flush | **The CPU's function at the flush**: exact by construction |
| Move the three-frame path of the `motion` twins onto the shared function too | One path per twin, less code | Changes when a GPU run's `motion2` becomes available (frame by frame today, at the flush then), which callers that read scores before the flush would notice; a deduplication step, not part of this port | Left for RC5 (`T-GPU-CUDA-HIP-DUPLICATED-KERNELS-2026-10-02`) |
| `motion_sycl`: a ring of three planes, as on CUDA | No extra copy per frame | The combined command graph is recorded for two slots and replayed; a ring of three does not fit two recordings, and tying the planes to the slot parity would silently turn the window into the three-frame one on a path where the slot does not alternate | Two fixed planes and two copies outside the graph |
| `motion_sycl`: read and overwrite one plane per slot inside the recorded graph | No extra plane or copy | The copy would have to run after the kernel that reads the same plane, inside a replayed graph whose replay of copy nodes the runtime notes call unreliable | Copies as plain queue commands behind the replay's barrier |

## Consequences

- **Positive**:
  - A GPU run of an `_hfr` model keeps its motion feature on the device, and
    `--feature motion=motion_five_frame_window=true` runs the twin.
  - Every output equals the CPU's bit for bit. Device tests
    (`test_<backend>_motion_five_frame_window`): six option sets on both
    extractors, sequences of 11, 1, 2 and 3 frames, 8 and 10 bits, `==` on
    every output of every frame, on an RTX 4090, an Arc A380 and a gfx1036.
    Clips at `--precision max` (Netflix 576x324 at 8 and 10 bits, a 1080p
    checkerboard pair, 50 frames of BBB 3840x2160; four option sets
    including an HFR model): 1352 of 1352 motion values identical on each of
    the three devices.
  - The twins of `motion_v2` lose their copies of the flush (45 to 62 lines
    net each on CUDA, SYCL and HIP).
  - Parity gate: `motion_mffw` and `motion_v2_mffw` report 0 against the CPU
    on CUDA, SYCL and HIP, on the Netflix pair (48 frames) and on 200 frames
    of BBB 3840x2160; the three-frame cells `motion`, `motion_debug` and
    `motion_v2` stay at 0. `motion_sycl` was measured with the combined
    command graph and without (`VMAF_SYCL_USE_GRAPH=1`,
    `VMAF_SYCL_NO_GRAPH=1`).
- **Negative**:
  - One more luma plane on the device while the option is set (CUDA and
    `motion_v2_sycl`: a third ring slot; HIP: a second kept plane).
    `motion_sycl` keeps its two planes and pays one more device copy of a
    luma plane per frame.
  - With the option the `motion` twins publish `motion2` and `motion3` at the
    flush, as the CPU does, not frame by frame.
- **Neutral / follow-ups**:
  - The Metal twins keep the CPU fallback until someone can measure a device
    implementation.
  - No SYCL kernel was added or changed, so the scratch-memory ratchet
    (ADR-1395), the sub-group sizes (ADR-1468) and the ahead-of-time targets
    are untouched.

## References

- `Q` (popup answer of the maintainer, 2026-10-02): "Port now, CPU and twins
  (Recommended)".
- [ADR-1478](1478-motion-five-frame-window-port.md) (the CPU port and the
  window function), [ADR-0219](0219-motion3-gpu-coverage.md) (its `-ENOTSUP`
  for the three twins of `motion` ends here),
  [ADR-1372](1372-cuda-motion-diff-first-pipeline.md),
  [ADR-1371](1371-sycl-motion-diff-first-pipeline.md),
  [ADR-1377](1377-hip-motion-diff-first.md) (the shared SAD kernels),
  [ADR-1108](1108-cuda-motion-v2-motion3-emission.md) (the `motion_v2` twins'
  flush), [ADR-1316](1316-gpu-option-value-capability-fallback.md),
  [ADR-1428](1428-exact-twins-fragments.md),
  [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-1468](1468-sycl-sub-group-sizes-every-aot-target.md).
- Netflix/vmaf [`a2b59b77`](https://github.com/Netflix/vmaf/commit/a2b59b77),
  [`a4a1492d`](https://github.com/Netflix/vmaf/commit/a4a1492d).
