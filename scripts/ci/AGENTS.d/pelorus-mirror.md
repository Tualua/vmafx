---
paths:
  - scripts/sync-pelorus-interop.sh
  - scripts/ci/pelorus*
  - scripts/ci/tests/test-sync-pelorus-interop.sh
  - scripts/ci/tests/test_pelorus_mirror.py
invariant: Mirror guard fails closed without the exact 40-character pin; CI checks out that object, never a branch or tag.
---
<!-- markdownlint-disable MD013 MD060 -->
# Pelorus mirror provenance gate (ADR-1113, ADR-1276)

`tests/test-sync-pelorus-interop.sh` proves the top-level mirror guard fails
closed for a plain directory and for a Git checkout lacking the exact pin. It
reconstructs source fixtures in disposable repositories, clears inherited
`GIT_*`, disables caller Git configuration, and proves the canonical fixture
prefix, tracked-path allowlist, and final-newline comparisons fail closed while
a synthetic re-pin/update refreshes every banner. The fixture uses a portable
Python byte rewrite, not platform-specific `sed -i`. Keep it wired into
required Pre-Commit through `.pre-commit-config.yaml`.

The real CI check belongs in the existing `Pre-Commit` job in
`lint-and-format.yml`: derive the 40-character pin from
`scripts/sync-pelorus-interop.sh`, check out `VMAFx/pelorus` at that object,
then run the script's default mode. Never replace the object with a branch/tag
or restore its working-tree fallback; a green check must bind every complete
rendered mirror and the exact tracked lint-exemption set to reviewed source
bytes.
