- **The production CPU and MCP server images carry the licences of what they
  contain, and publish the source their copyleft parts require.** Both images
  have `/usr/local/share/vmafx/licenses/THIRD_PARTY_NOTICES.txt` with every
  component, its licence and copyright notices, and the licence texts beside it;
  the VMAFx section is computed from the SPDX headers of every file the build
  compiled. The Debian sources of every installed package (and, for the server,
  the GCC source RPMs of the runtimes bundled in the numpy and scipy wheels) are
  published next to each image as `ghcr.io/vmafx/vmafx:<tag>-source` and
  `<tag>-server-source`, each platform image gets an attested SPDX SBOM, and
  the builds fail when a file of the image has no recorded licence. The server
  image no longer ships the Python build tools. `vmaf-mcp` and `vmaf-tune` now
  declare the licences their files carry (`EUPL-1.2`, and
  `EUPL-1.2 AND BSD-2-Clause-Patent` for `vmaf-tune`) and ship the texts in the
  wheel; the image labels name the VMAFx licence set instead of
  `BSD-2-Clause-Patent`, and the documentation footer states the per-file rule.
  The rules are [ADR-1513](docs/adr/1513-production-artifact-licensing.md); the
  audit of everything published before is
  [Research-2140](docs/research/2140-production-artifact-licence-audit.md)
  ([licensing](docs/licensing.md),
  [production images](docs/development/docker-production.md#licences-and-corresponding-source)).
