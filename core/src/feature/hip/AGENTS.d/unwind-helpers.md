---
paths:
  - core/src/feature/hip/integer_adm_hip.c
  - core/src/feature/hip/integer_cambi_hip.c
  - core/src/feature/hip/integer_ssim_hip.c
invariant: HISS-21 unwind helpers must release allocated device resources in reverse allocation order.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# HISS-21 unwind helpers — release order is the invariant (2026-09-21)

`chore/hiss21-core-src-hip` replaced the `goto`-based cleanup ladders in
`integer_adm_hip.c`, `integer_cambi_hip.c`, `integer_ms_ssim_hip.c`,
`integer_psnr_hvs_hip.c`, `speed_chroma_hip.c`, `speed_temporal_hip.c` and
`ssimulacra2_hip.c` with cascading `static` unwind helpers (HISS-01 / NASA
Rule 1), and split the oversized init / submit / collect / close / score-writer
functions into cohesive `static` helpers (HISS-04 / NASA Rule 4).

`integer_adm_hip.c` is the exception to the helper NAMES below, not to the
rules. #1507 rewrote that file's init and teardown while this branch was open,
so its release path is a straight-line cascade of paired acquire / release
helpers — `adm_hip_create_stream` / `adm_hip_destroy_stream`,
`adm_hip_load_modules` / `adm_hip_unload_modules`, `adm_hip_alloc_buffers` /
`adm_hip_free_buffers`, `adm_hip_alloc_luma` / `adm_hip_free_luma`,
`adm_hip_upload_buf` / `adm_hip_free_buf_dev` — driven by
`adm_hip_init_device()` and `init_fex_hip()`, with no `*_unwind_<label>()`
tier functions at all. The `adm_hip_unwind_*` names this section used to
document no longer exist; do not resurrect them. Every rule below still binds
that file: same release set, same release order, a real errno on every failure
exit, helpers `static` in the same TU, and no arithmetic expression split
across a helper boundary. The other six files keep the tier helpers as
described.

Rebase-sensitive invariants:

- **One helper per former label, tail-calling the next-earlier tier.** Each
  `*_unwind_<label>()` reproduces exactly one former `fail_<label>:` body and
  then calls the helper for the label it used to fall into. The release **set**
  and the release **ORDER** on every exit path must stay byte-for-byte what the
  ladder produced. Do not "simplify" a cascade into a single free-everything
  call: a tier may deliberately skip an earlier one (e.g. `speed_chroma`'s
  buffer-allocation failure leaves the module loaded and the stream alive,
  because those were claimed before the buffers and are released by a later
  failure, not this one).
- **A tier's `hipError_t` argument must be a real failure. Never
  `hipSuccess`.** Every ladder terminates in `hip_rc(rc)` / `ss2h_hip_rc(rc)`,
  which map `hipSuccess` to `0`. Handing a tier `hipSuccess` therefore makes
  the whole unwind return success: `init` reports 0,
  `vmaf_feature_extractor_context_init` sets `is_initialized`, and the first
  `extract` or `submit` runs against buffers, modules, events and a stream the
  ladder has already released. Three call sites did exactly that until T-HIP-INIT-UNWIND-REPORTS-SUCCESS-2026-09-22
  — `integer_adm_hip.c`'s feature-name-dictionary failure (independently fixed
  by #1507, whose rewrite of that file is what the tree now carries), and both
  allocator
  branches in `ssimulacra2_hip.c` — and an earlier revision of this file
  described the behaviour as pre-existing and not to be fixed inside a
  refactor. It was a use-after-free. When the failure is a host allocation with
  no `hipError_t` of its own, capture the ladder's result, and return the
  caller's own errno (`-ENOMEM` for the ADM dictionary;
  `ss2h_init_unwind_alloc()` forwards the allocator's) unless the ladder itself
  reports a genuine HIP error.
- **Release from the tier matching the LAST successful allocation.**
  `integer_adm_hip.c`'s dictionary failure used to skip `d_dis_luma` and
  `d_ref_luma`; the skip was inherited verbatim from a pre-HISS-01
  `goto fail_host` whose label sat below `fail_ref_luma:`. Because
  `vmaf_feature_extractor_context_close` rejects an uninitialised context,
  `close_fex_hip` never runs after a failed `init`, so nothing downstream
  reclaims a tier that is skipped — a skipped tier is a permanent leak, not a
  deferral. `init_fex_hip()` now releases the full set in exact reverse of the
  acquisition order (`adm_hip_free_buf_dev`, `adm_hip_free_luma`,
  `adm_hip_free_buffers`, `adm_hip_unload_modules`, `adm_hip_destroy_stream`)
  and returns `-ENOMEM`. Any new resource acquired in `adm_hip_init_device()`
  gets its release added to BOTH that list and `close_fex_hip()`.
- **Helpers stay `static` and in the same translation unit.** They exist so
  codegen stays equivalent to the inline code they replace. Do not give them
  external linkage, do not route them through function pointers, and do not
  move them to a shared header.
- **No arithmetic expression was split across a helper boundary and no
  accumulation order changed.** `ms_ssim_hip_set_max_db()` and
  `sc_score_channel()` were lifted at statement boundaries precisely so the
  scores stay bit-identical; `integer_adm_hip.c`'s score writers
  (`adm_hip_scale_scores()`, `adm_hip_append_scores()`) carry the same
  constraint under #1507's names.
  the HIP parity suite (`python3 "$(git rev-parse --show-toplevel)/scripts/ci/run_meson_test.py" -- -C <build> --suite hip`) before landing.
- **The twins stay recognisable.** These files are deliberate twins of
  `../cuda/*.c`. The unwind helpers mirror the CUDA labels one-for-one and keep
  the label names in the helper names, so a future CUDA-side port can be read
  against them.
- **The `VmafOption` tables and `g_weights[108]` stay one entry per line, with
  no `clang-format` fence.** `options_hip[]` (`integer_adm_hip.c`),
  `options[]` (`integer_cambi_hip.c`), `options_chroma[]` /
  `options_temporal[]` (the two SpEED HIP files) and `g_weights[108]`
  (`ssimulacra2_hip.c`) mirror their CPU and CUDA twins line for line, which
  is what makes a three-way `diff` of the ports readable. An earlier revision
  of this branch hand-packed them behind `// clang-format off` to shrink a
  HISS-04 "block" finding; the praetor engine no longer counts a file-scope
  initialiser table as a function, so the packing bought nothing and was
  reverted. Do not re-introduce it. Row content is load-bearing:
  `test_integer_adm_hip_option_table_mirrors_cpu` compares the ADM table
  against the CPU twin, so a rebase must preserve every name, alias, help
  string, default, range, flag and the array order.
- **The three SSIMULACRA2 no-split citations are withdrawn — see
  [ADR-1289](../../../../../docs/adr/1289-hip-ssimulacra2-host-helper-split.md).**
  `ss2h_picture_to_linear_rgb()`, `ss2h_run_scale_gpu()` and
  `extract_fex_hip()` used to carry an ADR-0141 §2 carve-out claiming that
  splitting them would break a line-for-line diff against the CPU source and
  the CUDA twin. Praetor's touched-file rule has no such carve-out, and the
  parity evidence is `core/test/test_hip_ssimulacra2_parity.c` plus the
  ADR-0214 cross-backend gate, not a diff, so the three are split into
  `ss2h_yuv_primaries()`, `ss2h_upload_xyb()`, `ss2h_download_blurred()` and
  `ss2h_downsample_for_next_scale()` and the `NOLINTNEXTLINE` lines are gone.
  What is still load-bearing: the ADR-1205 / ADR-0891 `fmaf()` chain in the
  per-pixel loop, the eight-launch order inside `ss2h_run_scale_gpu()`, and the
  per-scale order in `extract_fex_hip()`. `../cuda/ssimulacra2_cuda.c` and
  `core/src/feature/ssimulacra2.c` are still un-split and keep their own
  citations; when the CUDA HISS-21 slice lands it should mirror these four
  helper names and boundaries so the twins read against each other again.
  A split that leaves a no-split citation in place, or moves one onto the
  extracted helper, is still a defect — that is what ADR-1289 fixes here.
