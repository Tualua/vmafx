- **The tester image and the macOS tester bundle carry the licences of what
  they contain.** Each has `licenses/THIRD_PARTY_NOTICES.txt` (the container
  under `/opt/vmafx/licenses/`) with every component, its licence and
  copyright notices, and the licence texts beside it; the VMAFx section is
  computed from the SPDX headers of every source file the build compiled. The
  container's GPL and LGPL parts have their corresponding source published
  next to it as `ghcr.io/vmafx/vmafx:<version>-tester-source`, both packages
  get an attested SPDX SBOM, and both builds fail when a file of the package
  has no recorded licence (`tools/rc1-tester/image/licensing.json`). The image
  no longer strips the libraries the numpy, scipy, scikit-learn and Pillow
  wheels bundle, and the macOS bundle carries the licence texts of the
  libraries linked into its interpreter. The BRISQUE model is recorded under
  the LIVE laboratory's own notice (`LicenseRef-LIVE-BRISQUE`) and `mkdirp`
  under MIT. The rules every tester package follows, including the GPU and
  Windows kits, are
  [ADR-1503](docs/adr/1503-tester-artifact-licensing.md); the audit of the
  packages published before is
  [Research-2133](docs/research/2133-tester-artifact-licence-audit.md)
  ([tester guide](docs/usage/tester-image.md#licences-of-what-you-download)).
