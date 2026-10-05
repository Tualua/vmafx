- The nine PyTorch advisories without a fixed release (PYSEC-2025-189, -190,
  -192 to -197, -210) no longer reach any runtime package, and
  `security/vex/torch.openvex.json` records why the two training packages are
  not affected. `scripts/ci/check-torch-scope.py` keeps torch out of every other
  package; the triage process is in `docs/development/dependency-advisories.md`.
