<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1836: `vif_cuda` names its scores after `enable_chroma` when the caller sets it

- **Status**: Accepted (refines [ADR-0597](0597-integer-vif-luma-only-clarification.md))
- **Date**: 2026-10-05
- **Deciders**: lusoris
- **Tags**: `cuda`, `feature-extractor`, `vif`, `rc3`, `fork-local`

## Context

[ADR-0597](0597-integer-vif-luma-only-clarification.md) kept the CUDA VIF
twin's `enable_chroma` option as a documented no-op: `true` logs a warning and
the kernel stays luma-only. `init_fex_cuda()` (`core/src/feature/cuda/integer_vif_cuda.c`)
cleared the option before it built the feature-name dictionary, so
`--feature vif_cuda=enable_chroma=true` reported `integer_vif_scale0` to
`integer_vif_scale3`, the names of a default run.

Every other extractor builds its dictionary from the options as the caller
set them, and a non-default option adds its suffix to the names
(`vmaf_feature_name_from_options()`), so a caller finds a score under the name
it derives from the options it passed. Fixing the Metal CAMBI twin's name
order (#2132, `T-METAL-CAMBI-SCORE-NAME-SUFFIXED-2026-10-05`) led to a
device-free contract that checks this order for every CUDA, SYCL and HIP twin;
`vif_cuda` was its one violation. ADR-0597's own parity test,
`test_integer_vif_cpu_cuda_parity`, read the `enable_chroma=true` scores under
the default names, so the old order was in practice part of the contract.

## Decision

`init_fex_cuda()` builds the feature-name dictionary before it clears
`enable_chroma`. With `enable_chroma=true` the scores are reported as
`integer_vif_scale0_enable_chroma` to `integer_vif_scale3_enable_chroma`; their
values do not change (luma-only, ADR-0597), nor do the names of a run without
the option. `test_integer_vif_cpu_cuda_parity` reads the suffixed names and
refuses the default ones for that run; `test_gpu_twin_name_order_contract.py`
covers every CUDA, SYCL and HIP twin with no exception.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Name from the caller's options (chosen) | One naming rule for every extractor and twin; the contract has no exception; a caller finds the score under the name its options give | A user-visible output key changes for anyone passing `vif_cuda=enable_chroma=true` | — |
| Keep the default names (clear first) | No output change; the option stays a no-op in names too | `vif_cuda` is the one extractor whose names ignore an option it accepts; the order contract needs an exception | Rejected by the maintainer (popup, 2026-10-05) |
| Remove `enable_chroma` from `vif_cuda` | Nothing to name | Breaks callers that pass it, which ADR-0597 kept the option for | ADR-0597 stands |

## Consequences

- **Positive**: every twin's names follow the caller's options; one contract
  guards it everywhere.
- **Negative**: output key change for `vif_cuda=enable_chroma=true` (documented
  in `docs/metrics/vif.md` and the changelog).
- **Neutral / follow-ups**: the Metal twins join the same contract when the
  inline walk of `test_metal_twin_option_tables_contract.py` (#2132) moves to
  `core/test/feature_name_order.py`.

## References

- `Q1.2` (maintainer popup answer, 2026-10-05, verbatim): "Suffix and change the test".
- [ADR-0597](0597-integer-vif-luma-only-clarification.md), [ADR-1498](1498-metal-twins-exact-designs.md).
- `docs/state.md`: `T-GPU-VIF-NAMES-AFTER-OPTION-RESET-2026-10-05`, `T-METAL-CAMBI-SCORE-NAME-SUFFIXED-2026-10-05`.
