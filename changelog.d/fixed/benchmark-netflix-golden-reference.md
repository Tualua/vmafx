- **`testdata/benchmark_netflix.py` compares the src01 pair with the CLI golden.** The CPU row
  read `DIFF` against 76.66890519623612 (the Python harness's value); the reference is now
  76.66783025, the `vmafexec_test.py` assertion
  ([baselines](docs/development/netflix-benchmark-baselines.md)). FFmpeg patch impact: none.
