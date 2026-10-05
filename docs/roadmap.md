# Roadmap

VMAFx tracks its plan in GitHub, not in a document that drifts. This page is a
map of where that plan lives and how the releases are sequenced.

- **Board** — [VMAFx Roadmap](https://github.com/orgs/VMAFx/projects/1) (public)
- **Milestones** — [all milestones](https://github.com/VMAFx/vmafx/milestones)
- **Epics** — issues labelled
  [`epic`](https://github.com/VMAFx/vmafx/issues?q=is%3Aissue+is%3Aopen+label%3Aepic),
  each with a child task list

## Releases

| Milestone | Theme |
| --- | --- |
| [1.0.0](https://github.com/VMAFx/vmafx/milestone/1) | First release: RC1 correctness and tester reports, RC2 stabilisation, RC3 twin exactness, RC4 first full Rust metric and zero-copy import, RC5 deduplication, RC6 GPU capability table, RC7 CPU capability table, RC8 benchmarking and tuning, RC9 real model retraining, then final |
| [1.1](https://github.com/VMAFx/vmafx/milestone/2) | New metrics (ΔE-ITP, PU21, NIQE, BRISQUE, Y-FUNQUE+), their GPU twins, and the tools surface |
| [1.2](https://github.com/VMAFx/vmafx/milestone/3) | Cloud-native foundation: server mode, observability, containers and Kubernetes |
| [1.3](https://github.com/VMAFx/vmafx/milestone/4) | Cloud-native scale-out: operator, controller/node, multi-vendor GPU scheduling |
| [2.0](https://github.com/VMAFx/vmafx/milestone/5) | Language modernization — Go tools, Rust pilots, C++23 internals — completing the cloud-native arc |
| [Post-1.0 embedding](https://github.com/VMAFx/vmafx/milestone/8) | Embedding in encoders and media pipelines after 1.0.0: asynchronous window scores, Windows and macOS shared libraries and a CMake package ([ADR-1685](adr/1685-post-1-0-embedding-zero-copy-milestone.md); the zero-copy import API moved to RC4 by [ADR-1829](adr/1829-rc4-zero-copy-import.md), epic [#2067](https://github.com/VMAFx/vmafx/issues/2067)) |

Two milestones are deliberately **rolling** rather than tied to a release:

- [Models & benchmarks](https://github.com/VMAFx/vmafx/milestone/6) — retraining
  cadence, benchmark baselines, corpus work.
- [Code health & deduplication](https://github.com/VMAFx/vmafx/milestone/7) —
  the
  fork adds and changes a lot, so slimming it is recurring work, not a one-off.

## How 1.0.0 is gated

The fork's first release candidate, `v1.0.0-rc.1`, was published on
2026-09-27; older tags are inherited upstream history.
[ADR-1341](adr/1341-rc-correctness-benchmark-retrain-sequence.md) gives each
first-release candidate one responsibility.
[ADR-1421](adr/1421-rc3-rc8-candidate-map.md) maps the stages to tags, so that
each stage number matches its `v1.0.0-rc.N` tag (it supersedes the mapping of
[ADR-1352](adr/1352-rc-phase-shift-plus-one.md)), and
[ADR-1490](adr/1490-rc3-rc9-candidate-map-cpu-capability.md) inserts the CPU
capability stage as RC7, which moves benchmarking to RC8 and retraining to RC9.
[ADR-1868](adr/1868-candidate-map-2026-10-05.md) folds the work that joined
1.0.0 on 2026-10-05 into those candidates without new numbers: the new API and
provenance into RC4, tool consolidation, new metrics and the Metal SpEED twins
into RC5, training readiness into RC8.
[ADR-1880](adr/1880-format-envelope-device-targets.md) adds the format
envelope: an overflow audit at 8K and 16K and 8K exactness in RC3, the
supported resolutions, bit depths and layouts per backend and device in the
RC6 and RC7 tables, throughput per resolution in RC8; and device-targeted
scoring (one decode scored for several displays) in RC5.

### What this means for you

- **Use the newest candidate.** Each candidate keeps the Netflix golden scores;
  later candidates make GPU and SIMD results match the CPU and remove
  duplicated code.
- **No performance claims before RC8.** Candidates up to RC7 are about
  correctness; speed numbers are measured and tuned in RC8.
- **Model retraining comes last**, in RC9.
- **Help test.** Results from hardware the project does not own count as
  evidence: run the [tester image](usage/tester-image.md).

### The stages

| Stage | Theme | Tracking issue |
| --- | --- | --- |
| **RC1** | correctness and tester readiness | — |
| **RC2** | stabilisation and repair | — |
| **RC3** | twin exactness, overflow audit at 8K and 16K | [#1721](https://github.com/VMAFx/vmafx/issues/1721) |
| **RC4** | first full Rust metric, zero-copy device-frame import, new VMAFx API and FFmpeg filters, provenance | [#1723](https://github.com/VMAFx/vmafx/issues/1723) |
| **RC5** | deduplication, tool consolidation, new metrics with exact twins, Metal SpEED twins, device-targeted scoring | [#1724](https://github.com/VMAFx/vmafx/issues/1724) |
| **RC6** | GPU capability source of truth, GPU format envelope | [#1725](https://github.com/VMAFx/vmafx/issues/1725) |
| **RC7** | CPU capability source of truth, CPU format envelope | [#1885](https://github.com/VMAFx/vmafx/issues/1885) |
| **RC8** | benchmark and tune, throughput per resolution, training readiness | [#1245](https://github.com/VMAFx/vmafx/issues/1245) |
| **RC9** | real retraining | [#1246](https://github.com/VMAFx/vmafx/issues/1246), [#1242](https://github.com/VMAFx/vmafx/issues/1242) |
| **Final `v1.0.0`** | — | — |

#### RC1 — correctness and tester readiness

- **In scope:** Release-blocking correctness, reliability, security, build,
  packaging, backend usability, and a portable report path for outside hardware
- **Exit boundary:** The exact candidate head is green; no confirmed RC1 blocker
  or untriaged `docs/state.md` row remains; a tester can return
  artifact/environment identity, device and tool versions, backend availability,
  correctness/parity results, commands, logs, and failures

#### RC2 — stabilisation and repair

- **In scope:** The dependency updates and correctness fixes merged since rc.1,
  delivered to testers through the same report path; no benchmark or training
  work
- **Exit boundary:** The RC1 boundary, re-established on the rc.2 head

#### RC3 — twin exactness

- **In scope:** Every GPU and SIMD twin returns the CPU extractor's scores bit
  for bit, or carries a measured tolerance recorded in an ADR; no SYCL kernel
  uses scratch memory. Since 2026-10-05
  ([ADR-1880](adr/1880-format-envelope-device-targets.md)): an integer-overflow
  audit of every extractor and twin at 8K and 16K frame sizes with 16-bit
  samples
- **Exit boundary:** Per-twin parity measured at `--precision max` on the
  Netflix pairs, the 1080p checkerboard pairs, the 4K fixture and 8K cells;
  no accumulator overflows at 16K with 16-bit samples; Netflix golden
  assertions unchanged

#### RC4 — first full Rust metric and zero-copy import

Also in RC4 since 2026-10-05 (ADR-1868): provenance on every score
([#2142](https://github.com/VMAFx/vmafx/issues/2142)) through the new API.

- **In scope:** The whole `vmaf_v1.0.16_3d0h` path (cambi, speed_chroma, integer
  adm3, integer motion3, model prediction) in Rust; the C ABI is unchanged and
  the GPU twins stay CUDA, SYCL and HIP code. The whole device-memory import API
  ([ADR-1829](adr/1829-rc4-zero-copy-import.md)): additive import on
  `VmafPicture2` with fences in both directions for CUDA, SYCL, HIP and Metal,
  NV12 and P010 on the GPU, CUDA without its device-to-device copy, SYCL chroma
  import and D3D11, Metal IOSurface and MTLTexture bound without the CPU copy,
  a HIP import path, FFmpeg filters that take hardware frames. The new VMAFx C
  API (`vmafx/*.h`, `libvmafx.so.1`) generated with every other surface from
  `core/api/vmafx.toml`, `libvmaf.h` as a thin compatibility library on it, and
  the FFmpeg filters under VMAFx names (`vmafx`, `vmafx_tune`, `vmafx_pre`)
  ([ADR-1852](adr/1852-vmafx-api-redesign.md))
- **Exit boundary:** The Rust path is bit-identical to the C path on the parity
  fixtures; an imported device frame scores bit-identically to the same frame
  uploaded from the host, with no host copy of pixel data and fence-ordering
  tests that fail when a fence is skipped; the golden-data gate passes through
  the compatibility library and every generated surface is checked against the
  definition

#### RC5 — deduplication

- **In scope:** One implementation per behaviour across GPU twins and host code,
  the Rust code included; `libgpudispatch` extracted, folding in the
  per-backend import code RC4 wrote ([#1455](https://github.com/VMAFx/vmafx/issues/1455)).
  One implementation per tool ([#1249](https://github.com/VMAFx/vmafx/issues/1249)),
  the `tools/` surface finished and the known unfinished surfaces closed
  ([#1250](https://github.com/VMAFx/vmafx/issues/1250),
  [#1270](https://github.com/VMAFx/vmafx/issues/1270),
  [#1272](https://github.com/VMAFx/vmafx/issues/1272)). The new metrics with
  their twins written once on `libgpudispatch`: ΔE-ITP, PU21, NIQE, BRISQUE,
  Y-FUNQUE+ ([#1247](https://github.com/VMAFx/vmafx/issues/1247),
  [#1248](https://github.com/VMAFx/vmafx/issues/1248)), HDR-SSIM and
  HDR-MS-SSIM ([#2161](https://github.com/VMAFx/vmafx/issues/2161)), XPSNR
  ([#2158](https://github.com/VMAFx/vmafx/issues/2158)); Metal twins of
  `speed_chroma` and `speed_temporal`
  ([#2160](https://github.com/VMAFx/vmafx/issues/2160)). Device-targeted
  scoring ([ADR-1880](adr/1880-format-envelope-device-targets.md)): device
  profiles (phone, tablet, laptop, TV, VR per eye, portrait included), each a
  target resolution, a scaling and a viewing distance per display height mapped
  onto the ADM options `nvd` and `rdh`; one decode scored for many targets;
  a short research pass first, the profile table generated by the RC6 / RC7
  table machinery
- **Exit boundary:** Scores unchanged against the RC3 reference; duplicated code
  removed rather than moved; every new twin bit-identical to its CPU extractor
  or within a measured libm bound recorded in an ADR; a multi-target run scores
  each target as a separate run with that target's options would

#### RC6 — GPU capability source of truth

- **In scope:** A per-vendor capability table generated from
  `nvcc --list-gpu-arch`, `ocloc` and ROCm `llc -mcpu=help`, checked in with a
  CI drift check, covering the twins RC5 adds; dispatch and kernel parameters
  read it, with a generic fallback for unknown devices; every kernel compiled
  and statically audited for every target (scratch, spills, register ceiling,
  fp64). The table also declares the format envelope per backend and device
  ([ADR-1880](adr/1880-format-envelope-device-targets.md)): maximum resolution
  up to 16K with measured memory limits and tiling where needed, bit depths 8
  to 16, chroma layouts, odd and portrait sizes, each row backed by a test
- **Exit boundary:** Drift check green, the envelope included; audit clean for
  every listed target

#### RC7 — CPU capability source of truth

- **In scope:** The CPU twin of RC6: a checked-in table, generated by one
  script, of the CPU features each SIMD kernel needs (from the
  per-translation-unit compile flags in `core/src/meson.build` and the runtime
  gates in `core/src/x86/cpu.c` and `core/src/arm/cpu.c`), with a CI drift
  check; a per-function disassembly audit for x86 and aarch64; every dispatch
  level run bit-exact against scalar under emulation. The CPU format envelope
  (resolution up to 16K with measured memory limits, bit depths 8 to 16, chroma
  layouts, odd and portrait sizes) in the same table, each row test-backed
  ([ADR-1880](adr/1880-format-envelope-device-targets.md))
- **Exit boundary:** Drift check green; audit finds no instruction outside the
  feature set a gate guarantees; parity green under Intel SDE (AVX2-only model,
  Skylake-X, Ice Lake, Sapphire Rapids, the AMD AVX-512 set) and qemu (aarch64
  NEON, SVE2 at more than one vector length). Reports from real Xeon or Apple
  Silicon machines are extra evidence, not a requirement; timing is RC8

#### RC8 — benchmark and tune

- **In scope:** Comparable benchmark baselines, profiling, hardware-generation
  retuning, and measured performance fixes, including the speed RC3 gave up for
  exactness; throughput per resolution, 16K included (the envelope itself is
  RC6 / RC7 evidence). Training readiness: automatic temporal alignment and the
  HDR-input guard for SDR models
  ([#2163](https://github.com/VMAFx/vmafx/issues/2163),
  [#2157](https://github.com/VMAFx/vmafx/issues/2157)), the external-metric
  runner and estimator calibration
  ([#2162](https://github.com/VMAFx/vmafx/issues/2162),
  [#2143](https://github.com/VMAFx/vmafx/issues/2143)), the HDR
  conversion-check workflow ([#2145](https://github.com/VMAFx/vmafx/issues/2145)),
  the mini retrain in CI and the measured resource plan of
  [#1246](https://github.com/VMAFx/vmafx/issues/1246)
- **Exit boundary:** Results identify the exact artifact, fixtures, host,
  drivers and runtimes; accepted wins are re-measured and preserve
  correctness/parity; the mini retrain passes every stage

#### RC9 — real retraining

- **In scope:** The locked one-shot model retraining programme on the clean,
  tuned tree, started only when every precondition of
  [#1246](https://github.com/VMAFx/vmafx/issues/1246) holds (the RC4 to RC8
  items above included)
- **Exit boundary:** Model-quality gates, model cards, registry/signing
  metadata, and unchanged Netflix golden assertions pass

#### Final `v1.0.0`

- **In scope:** Accepted RC9 output plus any required repair candidate
- **Exit boundary:** Publication preflight passes on the immutable final tag

“Done fixing” is deliberately bounded rather than a promise that no future bug
will be found. RC1 and RC2 are ready when there are no confirmed, actionable
release blockers and no untriaged rows. Performance-only findings belong to
RC8, real training belongs to RC9, and externally blocked work stays explicitly
deferred with its trigger and evidence.

If any later candidate exposes a correctness regression, fix it and rerun the
affected stage evidence before proceeding. Do not pull general benchmarking
into RC1 to RC7, or real training before RC8 evidence is accepted. Speed that
RC3 gives up for exactness is recorded as a tuning row and recovered in RC8; it
is not traded back for a tolerance. The RC1 and RC2 report envelope may run a
short correctness and device-engagement smoke; it does not make a performance
claim.

Ordinary Renovate and other version PRs remain mergeable throughout the
sequence when normal required checks, review, pinning, and component-specific
validation pass. Security updates are prioritised rather than being the only
allowed updates. Because evidence is exact-head, any later merge requires the
affected candidate checks or measurements to be rerun.

## Things that do not change

Some guarantees are load-bearing for downstream users and hold across every
milestone above, including 2.0:

- The **Netflix golden values** are never edited. They are the numerical
  ground truth; if scores drift, the code is wrong.
- The **`libvmaf.so` ABI** and the FFmpeg `libvmaf` filter name stay stable,
  even
  as the internals move to C++23 and parts of the tooling move to Go and Rust.
- The public **C API** under `core/include/libvmaf/` stays source-compatible.
- Release artifacts are **built in the container**, never from a host build.

## Contributing against the roadmap

Pick an epic, read its task list, and open a PR that closes one line of it.
Epics
are intentionally coarse — sub-tasks become their own issues when someone starts
them, so the tracker reflects work in progress rather than a wish list.
