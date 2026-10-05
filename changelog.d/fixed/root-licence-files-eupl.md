- **The repository root states the fork's licence, and every package declares
  the licences of the files it ships.** `LICENSE` is now the EUPL-1.2, the
  licence of the files the fork wrote; Netflix's BSD-2-Clause-Patent text and
  copyright notice moved unchanged to `NOTICE`, a name licence detectors do not
  read as a licence file, so `LICENSE` is the only root licence file (licensee
  9.18.0 reports the project as EUPL-1.2; on master it found an MIT licence). The `vmafx-sys` and `vmafx` crates declare `EUPL-1.2`
  instead of `BSD-2-Clause-Patent`, the Helm chart's `artifacthub.io/license` is
  `EUPL-1.2`, the vendored Prometheus Pushgateway subchart is recorded as
  Apache-2.0, `vmafx-rc1-tester` declares `EUPL-1.2 AND MIT`, and the fork's
  `.toml` manifests and configuration files carry an SPDX header (two
  `pyproject.toml` files moved from `BSD-2-Clause-Patent` to `EUPL-1.2`), and the
  dev image's licence label names the licences of the VMAFx files it copies.
  `scripts/ci/check_licence_metadata.py` holds the root files and every manifest
  to the files in the required `Licence Provenance` check and on every commit.
  Published artifacts keep carrying the same licence texts
  ([licensing](docs/licensing.md), ADR-1699).
