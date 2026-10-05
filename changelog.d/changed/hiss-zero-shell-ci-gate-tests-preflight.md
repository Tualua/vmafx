- **The CI gate tests, preflight and the RC3 retest runner meet the HISS shell
  rules (ADR-1142).** The shell tests of the CI gates handle the exit status of
  the commands they used to discard, `scripts/dev/preflight.sh` and
  `scripts/dev/rc3-home-gpu-retest.sh` run under `set -euo pipefail` (every
  probe whose non-zero status means no hit now says so), and
  `scripts/ci/tests/test-preflight-msvcism.sh` pins that the msvcism stage still
  fails on a planted `nullptr` and a single-paren `__attribute__`. The HISS
  baseline loses 18 infractions.
