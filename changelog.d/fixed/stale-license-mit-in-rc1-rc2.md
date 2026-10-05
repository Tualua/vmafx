- **`LICENSE-MIT` in 1.0.0-rc.1 and 1.0.0-rc.2 was stale.** The trees of both
  release candidates held a root `LICENSE-MIT` ("Copyright (c) 2026 Lusoris"),
  a leftover of ADR-0686's plan to dual-license fork code under
  BSD-3-Clause-Plus-Patent or MIT, which ADR-1250 replaced before the first
  candidate. ADR-1250 governs: fork-authored code is EUPL-1.2, Netflix's code is
  BSD-2-Clause-Patent, and each file's SPDX header is authoritative. The file is
  removed, the root `go.mod` retracts `v1.0.0-rc.1` and `v1.0.0-rc.2`, and the
  tags stay. Copies remain in GitHub's source archives of the tags made between
  2026-05-28 and 2026-10-05 and in the Go module proxy's zips of both versions;
  no published image, release file, tester bundle or Python package contained
  it ([licensing](docs/licensing.md#the-stale-license-mit-in-their-source-trees),
  ADR-1699).
