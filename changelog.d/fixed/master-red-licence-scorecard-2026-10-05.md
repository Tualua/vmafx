- **Seven Metal helper headers carry the licence of the Netflix code they reproduce, and the tester bundles are signed in a form Scorecard counts.**
  Five `core/src/feature/metal/metal_*_math.h` headers now list `Copyright 2016-... Netflix, Inc.` and
  `EUPL-1.2 AND BSD-2-Clause-Patent` like their CUDA, HIP and SYCL twins, and two are recorded as
  reproducing none of it ([ADR-1250](docs/adr/1250-eupl-fork-relicense.md),
  [ADR-1474](docs/adr/1474-relicense-helper-headers-and-ci-check.md)). The macOS and Windows tester
  workflows now write each Sigstore signature as `<asset>.sigstore.json` instead of `<asset>.bundle`;
  verify with `cosign verify-blob --bundle <asset>.sigstore.json`
  ([tester guide](docs/usage/tester-image.md)). Already published tester releases keep the old name.
