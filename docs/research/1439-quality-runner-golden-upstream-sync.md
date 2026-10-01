<!-- markdownlint-disable MD013 MD041 MD060 -->
# Research-1439: `quality_runner_test.py` vs Netflix upstream

Companion digest for [ADR-1439](../adr/1439-quality-runner-golden-upstream-sync.md).

## Method

Both files were parsed with Python's `ast` module. For every test method,
every `assertAlmostEqual(<expr>, <literal>, places=N)` was keyed by its first
argument (quote style normalised) and compared by value and `places` with the
same key in Netflix `upstream/master:python/test/quality_runner_test.py`.

## Findings (before the sync)

- 33 test methods, 115 shared assertions differ from upstream.
- Five upstream test methods have no fork counterpart (not in scope).
- Typical pattern: the fork value predates an upstream test update and the
  assertion was loosened so it still passed, for example
  `test_run_vmaf_runner_3threads` `VMAF_score` 76.66890519623612 at `places=2`
  (upstream 76.66783025 at `places=4`), `test_run_vmaf_runner` `motion2`
  3.8953518541666665 at `places=2` (upstream 3.8943597291666667 at
  `places=4`), `test_run_vmaf_runner_v061` float `vif_scale0` 0.363420489439
  at `places=3` (upstream 0.3636595790491415 at `places=4`).
- The upstream changes behind them: Netflix `a44e5e611` (integer motion edge
  mirroring) and `142c06714` (float VIF on-the-fly kernel), both already in
  the fork's code.

## Sync

224 literal replacements (values and `places`), each copied from the upstream
source text at the AST position of the argument; no other byte of the file
changed. After the sync, the same comparison reports no difference.

## Verification

Full `quality_runner_test.py` in the SYCL build image (GCC 15.2, icx/icpx
2026.1.0), `--cpumask 16`, Netflix test YUVs:

| Build | Result |
|---|---|
| GCC (`CC=gcc CXX=g++`) | 61 passed, 1 skipped |
| icx / icpx | 61 passed, 1 skipped |

## Reproducer

```bash
git show upstream/master:python/test/quality_runner_test.py > /tmp/up.py
python3 -m pytest python/test/quality_runner_test.py -q
```
