---
paths:
  - core/src/feature/cuda/integer_psnr_cuda.c
  - core/src/feature/cuda/integer_psnr_cuda.h
invariant: Integer PSNR honours enable_chroma parity and zeroes accumulators on picture streams.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Integer PSNR enable_chroma and CPU option tables

- **`integer_psnr_cuda.c` honours `enable_chroma` option parity** (ADR-0453).
  `enable_chroma` option (default `true`) clamps `n_planes` to 1 in
  `init_fex_cuda` when set to `false`, matching CPU
  `integer_psnr.c::init`'s behaviour. Clamp runs after
  `pix_fmt == YUV400P` guard so YUV400 sources always luma-only
  regardless of option. On rebase: if upstream Netflix adds
  `enable_chroma` option to CPU path behaving differently from
  fork's GPU guard, audit both, keep GPU clamp semantically
  equivalent. SYCL and Vulkan twins carry identical guard, must
  move in lockstep with any change to this one. Cross-backend parity
  gate at `places=4` covers both `enable_chroma=true` (default) and
  `enable_chroma=false` paths.

- **Option tables = CPU tables** (ADR-1373) on `psnr_cuda`,
  `integer_ssim_cuda`, `float_ssim_cuda`, `float_motion_cuda`. Host
  arithmetic = CPU helper, never copy: `psnr_score.h`
  (`vmaf_psnr_peak/max/from_mse/aggregate`, plus `flush` for `apsnr_*`),
  `vmaf_ssim_max_db()` + `nonfinite_score.h` emitters, `motion_clip()`.
  `test_twin_options_are_cpu_options` rejects twin-only keys; sole
  exception `float_ssim_cuda` `enable_chroma` (accepted no-op, HISS-14,
  warns when set). Never remove it without `!` + `Migration:`.
- **`psnr_cuda`: every plane accumulator zeroed on picture stream** (the
  kernels' stream; memset on `lc.str` races the atomic adds,
  `kernel_template.h`). **`psnr_cuda` is TEMPORAL** like CPU `psnr`: else
  `--subsample N` feeds 1 frame in N into `apsnr_*`.
