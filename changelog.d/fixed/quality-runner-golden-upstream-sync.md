- **`quality_runner_test.py` asserts Netflix's current golden values again.**
  33 test methods (115 assertions) still held values from before two upstream
  test updates (`a44e5e611` integer-motion edge mirroring, `142c06714` float
  VIF on-the-fly kernel) and had been loosened down to `places=1` so they kept
  passing. Every assertion shared with Netflix `upstream/master` now carries
  upstream's exact value and `places`; the full suite passes on GCC and icx
  builds ([ADR-1439](docs/adr/1439-quality-runner-golden-upstream-sync.md)).
