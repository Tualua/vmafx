---
paths:
  - .github/workflows/libvmaf-build-matrix.yml
  - scripts/ci/run_meson_test.py
  - core/test/test_meson_secret_env_sanitization.py
invariant: Every workflow step running Meson/Ninja test invokes run_meson_test.py; resolve Python before sudo.
---
# Meson parent-environment sanitization (ADR-1333)

Every workflow step that runs Meson test suite or Ninja's `test` target
must invoke `scripts/ci/run_meson_test.py` instead. Linux steps that need
`sudo` resolve both Python and Meson before elevation and pass Meson with
`--meson-executable`; Windows uses checked-in Python wrapper path.
`core/test/test_meson_secret_env_sanitization.py` inventories each call and
rejects raw bypass. Preserve wrapper boundary because Meson writes its
parent environment to `testlog.txt` before applying default setup.
