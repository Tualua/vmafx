<!-- markdownlint-disable MD013 -->
# Test fixtures

Run `scripts/test/fetch-test-yuvs.sh` once per checkout to provision the
YUV clips that the Python tests (`python/test/quality_runner_test.py`,
`python/test/feature_extractor_test.py`) and the Netflix golden gate read.

The clips live outside the repository. Netflix removed them upstream in
2020 (commit `bac8b6073`) and moved them to the sibling repository
<https://github.com/Netflix/vmaf_resource>. The local target directory
`python/test/resource/yuv/` is gitignored, so it is absent in a fresh
tree.

## Provisioning fixtures locally

```bash
scripts/test/fetch-test-yuvs.sh
```

The script:

- Downloads five files into `python/test/resource/yuv/` when they are
  not already present: `src01_hrc00_576x324.yuv` and
  `src01_hrc01_576x324.yuv` (576x324), and
  `checkerboard_1920_1080_10_3_0_0.yuv`,
  `checkerboard_1920_1080_10_3_1_0.yuv` and
  `checkerboard_1920_1080_10_3_10_0.yuv` (1920x1080).
- Verifies the md5 sum of every file against hardcoded expected values.
  A local file with the right name but the wrong content is detected,
  deleted and downloaded again.
- Is idempotent. Re-running on a fully provisioned tree prints
  `ok      <name> (md5 verified)` for each fixture and exits 0.

These five files are the three Netflix test pairs of the CPU golden gate
(normal, checkerboard 1-px, checkerboard 10-px; see section 8 of
[AGENTS.md](https://github.com/VMAFx/vmafx/blob/master/AGENTS.md) and
[ADR-0024](../adr/0024-netflix-golden-preserved.md)). The cross-backend
VIF and motion diff jobs that CI runs use them too.

## Why md5 verification matters

The script refuses a file whose md5 differs from the expected value and
fetches it again. A mismatch is reported at provision time:

```text
stale   src01_hrc00_576x324.yuv (md5 4226fb7e…, want b16f67d3…) — refetching
```

!!! note
    A stale local copy with the canonical name and size but different
    content once produced 84 test failures in the
    `feature_extractor_test.py` and `quality_runner_test.py` suites
    (2026-05-17). The assertion errors looked like extractor bugs (VIF
    score about twice the expected value), but the cause was a fixture
    content mismatch. See
    [ADR-0493](../adr/0493-test-yuv-fixture-md5-verification.md).

## CI parity

GitHub Actions runs an equivalent inline `curl` block in
[`.github/workflows/tests-and-quality-gates.yml`](../../.github/workflows/tests-and-quality-gates.yml).
If the canonical content in `Netflix/vmaf_resource` changes, update both
the CI workflow and the script's expected-md5 list in the same change.

## Fixtures not covered by the script

Some tests reference fixtures that CI does not run on every PR and that
the script does not provision:

- `KristenAndSara_1280x720_8bit_processed.yuv`
- the multi-frame and bit-depth `src01_*` variants

When a CI job activates one of those tests, add the file with its md5 to
the `FIXTURES` array in `scripts/test/fetch-test-yuvs.sh`.

## Other fixture roots

- `testdata/` holds the tracked 48-frame pairs
  (`ref_576x324_48f.yuv` / `dis_576x324_48f.yuv` and the 640x480 pair)
  and the snapshot JSONs.
- `testdata/bbb` is a gitignored 4K clip used by the performance
  baselines; see
  [backend performance baselines](backend-perf-baselines.md).
- `python/test/resource/yuv/` is the directory this script fills.

## Related

- [AGENTS.md](https://github.com/VMAFx/vmafx/blob/master/AGENTS.md)
  section 8: Netflix golden-data gate rule
- [ADR-0024](../adr/0024-netflix-golden-preserved.md): Netflix golden tests
- [ADR-0493](../adr/0493-test-yuv-fixture-md5-verification.md): rationale for
  this provisioner
