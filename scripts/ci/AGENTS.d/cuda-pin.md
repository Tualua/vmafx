---
paths:
  - scripts/ci/check-cuda-pin-lockstep.py
  - scripts/ci/install-cuda-toolkit.*
  - scripts/ci/tests/test_cuda_pin_single_source.py
  - scripts/ci/tests/test_install_cuda_toolkit.py
invariant: One CUDA release = seven literals owned by `build-config.env` `CUDA_VERSION`; never narrow the residual sweep.
---
<!-- markdownlint-disable MD013 MD060 -->
# CUDA coordinated pin (ADR-1285)

One CUDA release = seven literals across two files. Authority =
`build-config.env` `CUDA_VERSION`; that file also records the apt package
series, a release-review latch, and exact toolkit/nvcc/cudart Debian versions.
`check-cuda-pin-lockstep.py` checks all seven, `--write` derives only the apt
series and OCI-description spellings, and a residual sweep fails on any CUDA
release literal in an unrecognised spelling.
Never narrow the sweep to silence a new site: teach the gate its shape and
owner in the same change, or the site drifts. `--write` must never touch
`CUDA_VERSION` or the exact apt metadata. Component build numbers are not a
function of the marketing release; refresh them from NVIDIA's live redist
manifest and Ubuntu Packages index. `CUDA_APT_LOCK_RELEASE` deliberately stays
outside Renovate ownership, so a bot bump fails until that review happens.
The shared installer must keep exact `package=version` apt operands, subsequent
`dpkg-query` validation, and all three builder/runtime/full modes. Its fake-host
contract suite has the dedicated `test-install-cuda-toolkit` pre-commit hook,
which the required Pre-Commit workflow runs. Renovate resolves `CUDA_VERSION` through
`custom.nvidia-cuda-redist`: the official HTML index plus an exact
`redistrib_X.Y.Z.json` extractor. Never restore the old `nvidia/cuda` package
group. The custom feed has no timestamps, so its narrowly matched rule stays
timestamp-optional, manual-review, and non-automerge. Fixture:
`tests/test_cuda_pin_single_source.py`, run by the
`test-base-image-single-source` hook.
