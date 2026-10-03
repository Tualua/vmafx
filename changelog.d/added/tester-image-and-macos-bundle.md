- **A tester image and a macOS bundle let someone outside the project test the fork
  without building it, and send the result for credit.** `ghcr.io/vmafx/vmafx:<tag>-tester`
  (linux/amd64 and linux/arm64) and a macOS arm64 `.tar.gz` each run one command that
  prints a JSON report: host facts, every CPU extractor at `--precision max` with default
  dispatch against scalar C, baked reference scores, SIMD unit tests, and the Netflix
  golden gate (image) or every Metal twin against the CPU (bundle). The container runs
  under `--network none --read-only --cap-drop ALL`; both packages are built only by
  hosted workflows from a tagged commit and carry cosign signatures and build
  provenance. Reports are added as `docs/hardware-reports/<date>-<cpu>.json`, checked by
  `scripts/ci/check-hardware-reports.py`. See
  [the tester guide](docs/usage/tester-image.md),
  [ADR-1492](docs/adr/1492-tester-image-arm64-report.md) and
  [ADR-1493](docs/adr/1493-macos-tester-bundle.md).
