<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1595: SYCL zero-copy input fails loudly and the FFmpeg filter routes `feature=` names to SYCL twins

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `sycl`, `ffmpeg`, `zero-copy`, `correctness`, `testing`, `fork-local`

## Context

With QSV-decoded frames the `libvmaf_sycl` filter imports the VA surface into
device memory and calls `vmaf_read_pictures_sycl`, which hands the extractors
no host pictures. Observed on an Intel Arc A380: the model features (`vif`,
`adm`, `motion`) are bit-exact against the CPU, but `feature=name=psnr` or
`name=cambi` silently produces no `psnr_*` / `cambi` output. Three causes:

1. `read_pictures_sycl_extractors` (`core/src/libvmaf.c`) runs only extractors
   flagged `VMAF_FEATURE_EXTRACTOR_SYCL` and `continue`s past the others, so a
   CPU extractor registered by the filter's `parse_features` is dropped with
   no diagnostic.
2. Registered SYCL extractors get `submit(NULL, NULL, NULL, NULL)`; six
   dereference the NULL picture, five return `-EINVAL`, and `motion_sycl` with
   `motion_add_uv` reads stale chroma without any error.
3. On a VA-import failure the filter warns and skips the frame, which silently
   changes the pooled score.

The FFmpeg patch stack never calls `vmaf_feature_backend_twin` (ADR-1359),
which the CLI already uses to pick a SYCL twin for a CPU feature name.

Toolchain note (project rule 15): this phase builds and tests in the
`localhost/vmafx:build-ocloc` toolchain image through its own launcher
`scripts/test/sycl-dev-container.sh` instead of `vmaf-dev-mcp`, because master
has no `scripts/test/lib/container-rt.sh` / `Containerfile.vmafx` yet (the
ADR-1441 branch is unmerged), the work needs the oneVPL GPU runtime and the
QSV ffmpeg build dependencies that image carries, and the phase publishes no
artifacts (the rule-15 canonical-artifact clause is not triggered).

## Decision

We will make every unsupported zero-copy situation an immediate, named error:

- `vmaf_read_pictures_sycl` runs a pre-pass guard
  (`sycl_check_zero_copy_extractors`) as its first statement, before
  `vmaf_sycl_queue_wait`, `pic_cnt++` and `vmaf_sycl_advance_frame`. A
  registered extractor without `VMAF_FEATURE_EXTRACTOR_SYCL` returns
  `-ENOTSUP` and logs its name (and its SYCL twin when one exists), so no
  half-advanced frame is left behind (D-03).
- Each SYCL extractor that cannot run without host pictures calls a per-extractor
  helper `vmaf_sycl_require_host_pictures` and returns `-ENOTSUP` (not
  `-EINVAL`) with a log line naming the feature. `motion_add_uv` stale chroma
  becomes this error too (D-03).
- A VA-surface import failure in the `libvmaf_sycl` filter is fatal (D-02).
- FFmpeg patch 0005 routes `feature=` names to SYCL twins through
  `vmaf_feature_backend_twin` on both paths. Zero-copy: no twin or `-ENOTSUP`
  is a fatal error. Host-upload: fall back to the CPU extractor with a
  warning, like the CLI. The host-upload switch to twins is proven bit-exact
  against the CPU by test (D-04).
- Chroma is imported eagerly on every zero-copy frame (D-01, later plans).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Central capability flag checked in `submit_nocopy` | One place | Cannot express option-dependent needs (`psnr enable_chroma`, `motion_add_uv`); adds a bit to a fork-modified enum | Per-extractor helper chosen; a flag may be added later |
| Hook in upstream `parse_features` | One implementation of the parsing | Touches shared upstream code in the patch | Chosen, with a dedicated `parse_features_sycl` as fallback if the hunk conflicts |
| Duplicate `parse_features_sycl` | No change to shared code | ~50 duplicated lines | Fallback only |
| Keep warn-and-skip on import failure | No behaviour change | Pooled score silently covers fewer frames | Rejected (D-02) |
| `-EINVAL` for missing host pictures | Status quo for five extractors | Indistinguishable from bad arguments | `-ENOTSUP` chosen (D-03) |

## Consequences

- **Positive**: no silent drop, no NULL dereference, no stale chroma on zero-copy.
- **Negative**: until chroma import lands, default `psnr` on zero-copy fails
  loudly instead of omitting `psnr_*`. Docs quoting `-22` change in the same PR.
- **Neutral / follow-ups**: `test_sycl_zerocopy_guards`, the launcher and its
  dev page ship with this ADR; later plans extend the guard to every SYCL
  extractor and add chroma import.

## References

- `req` (user, 2026-10-02), D-01: "Chroma: allocate the shared chroma planes eagerly in `vmaf_sycl_init_frame_buffers` and import VA UV (`layers[1]`) on every zero-copy frame (no "chroma needed" gating)."
- `req` (user, 2026-10-02), D-02: "A VA-surface import failure in the `libvmaf_sycl` filter is fatal (no more warn-and-skip-frame)."
- `req` (user, 2026-10-02), D-03: "An extractor that cannot run without host pictures returns `-ENOTSUP` (not `-EINVAL`) on the zero-copy path, with a log line naming the feature; docs that quote the current `-22` text are updated in the same PR."
- `req` (user, 2026-10-02), D-04: "`libvmaf_sycl` routes `feature=` names to SYCL twins via `vmaf_feature_backend_twin` on BOTH paths: zero-copy (no twin / -ENOTSUP = fatal error) and host-upload (fall back to the CPU extractor with a warning, like the CLI). The host-upload switch to twins must be shown bit-exact vs CPU by test."
- [ADR-1359](1359-cli-feature-backend-twin.md), [ADR-1369](1369-sycl-shared-planes-light-twins.md), [ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md).
