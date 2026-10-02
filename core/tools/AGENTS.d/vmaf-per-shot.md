---
paths:
  - core/tools/vmaf_per_shot.c
  - core/tools/vmaf_per_shot_input.c
invariant: vmaf-perShot is standalone sidecar; --help short-option is -H; scan stops at VMAF_PER_SHOT_MAX_FRAMES or --frames.
---
# vmaf-perShot sidecar predictor contract

- [ADR-0222](../../../docs/adr/0222-vmaf-per-shot-tool.md) — `vmaf-perShot`
  per-shot CRF predictor sidecar (T6-3b).
  - **Sidecar invariant**: this binary is **standalone** —
    does not link libvmaf metric path; its output is
    encoder hint, not quality score. Any future
    integration must keep per-shot prediction outside
    `vmaf_score_*` to preserve roadmap §2.4's separation.
  - **Schema invariant**: CSV / JSON columns
    (`shot_id`, `start_frame`, `end_frame`, `frames`,
    `mean_complexity`, `mean_motion`, `predicted_crf`)
    stable across v1; v2's trained MLP must reuse
    them to avoid downstream encoder churn.
  - **Input invariant**: `--pixel_format 420|422|444` only changes
    planar chroma-byte skipping. Per-shot detector and predictor
    remain luma-only, and high-bit-depth inputs use little-endian
    16-bit sample containers for `--bitdepth 10|12|16`.
  - **`--help` short-option is `-H`, NOT `-?`** (rebase-sensitive).
    getopt returns `'?'` for any unrecognised option; if `--help` maps
    to `'?'` two cases become indistinguishable, unknown flags
    silently succeed. `per_shot_long_opts` table maps `--help` to
    `'H'`; `per_shot_parse_args` handles `'H'` for help and `'?'` for
    error path. Never change short-option value.
  - **Scan stops at `VMAF_PER_SHOT_MAX_FRAMES` or `--frames` ceiling.** `per_shot_scan_loop`
    tracks frames in `uint64_t` and `per_shot_record_frame` stores that
    index. explicit operator ceiling `-F, --frames <N>` (with aliases
    `--frame_cnt` and `--max-frames`) bounds scans on FIFOs, streams, or
    synthetic inputs, exiting cleanly with code 0 on reaching N frames
    ([ADR-1318](../../../docs/adr/1318-pershot-frames-ceiling.md)). default
    is `0U` (unbounded), preserving full scans on finite files up to
    `VMAF_PER_SHOT_MAX_FRAMES` (`UINT32_MAX`), where exhaustion reports `-EFBIG`.
    Never restore bare `for (;;)`. At built-in boundary, reader probes
    for one additional *complete* frame and checks that read before indexing it:
    input of exactly `UINT32_MAX` frames is accepted when EOF is reached,
    reporting `-EFBIG` only if input strictly exceeds `UINT32_MAX` complete
    frames, resolving off-by-one check from
    [ADR-1287](../../../docs/adr/1287-cli-tool-unbounded-loop-ceilings.md).
  - **Raw-frame reads consume every luma and chroma byte** (rebase-sensitive).
    `vmaf_per_shot_read_luma` treats EOF as clean only before first luma
    byte of new frame. short luma plane, short chroma planes, or `ferror`
    fails closed. Never restore seek-based chroma skipping: ISO C permits
    regular file seek beyond EOF, so seek success does not prove that raw
    frame is complete and can create phantom final frame.
