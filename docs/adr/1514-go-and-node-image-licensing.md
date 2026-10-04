<!-- markdownlint-disable MD013 MD060 -->
# ADR-1514: The Go service images record every linked module's licence from the binary, and the node image ships a redistributable FFmpeg, a source-built rclone and the records of the libraries it copies

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: license, compliance, supply-chain, docker, go, ffmpeg, fork-local

## Context

[ADR-1513](1513-production-artifact-licensing.md) applies the tester licensing
rules to the production artifacts. The audit behind it
([Research-2140](../research/2140-production-artifact-licence-audit.md)) found
three problems specific to the Go service images (`vmafx-operator`,
`vmafx-server`, `vmafx-node`):

- Each Go program statically links 177 to 183 modules (Apache-2.0, 31 of them
  with a `NOTICE` file, MIT, BSD, ISC, and MPL-2.0 modules of riverqueue and
  hashicorp) and the images carried none of their licence texts, no NOTICE file
  and no information on where the MPL-2.0 source is (MPL-2.0 3.2(a)).
- The node image's FFmpeg was configured `--enable-nonfree`. No nonfree
  component was enabled, but the published binary prints "This version of
  ffmpeg has nonfree parts compiled in. Therefore it is not legally
  redistributable." The build is otherwise GPL-3.0-or-later (x264 and x265 are
  GPL-2.0-or-later), and neither its source nor that of its 40 dependency
  libraries, copied out of their Debian packages into the distroless image
  without their copyright files, was published.
- The node image copied the `rclone` binary out of the official image. It links
  `github.com/cloudsoda/sddl` (LGPL-3.0) and MPL-2.0 modules, and its build
  information says `vcs.modified=true`: the tree it was built from is not the
  release's, so its corresponding source cannot be identified.

## Decision

1. **Go modules are read from the binary.** `licensing.py` parses the
   `.go.buildinfo` section of each shipped Go program (main module,
   dependencies, replacements, `h1:` sums). In the build stage `go-licences`
   copies every module's `LICENSE*`, `COPYING*`, `NOTICE*` and `PATENTS*` files
   out of the module cache into the image (`licenses/go/<module>@<version>/`);
   `scan-go` reads the SPDX headers of our own Go files the program compiles
   (`go list -deps`). The gate fails on a module without a text, on a licence it
   cannot classify (unless `go_module_licences` records it), and on a recorded
   program missing from the image. The `-source` image holds the module zip of
   every copyleft module (MPL-2.0, EUPL-1.2, GPL, LGPL), and of every module of
   a program that links an LGPL or GPL module (LGPL-3.0 4(d): the application in
   a form that can be relinked), fetched from the module proxy and refused
   unless its dirhash equals the binary's `h1:` sum.
2. **FFmpeg is built without `--enable-nonfree`** and published under
   GPL-3.0-or-later with its licence files; the `-source` image holds the
   patched tree exactly as compiled (`git archive` after `git am`), the patch
   series and the configure line.
3. **Copied libraries keep their records.** `scripts/ci/record-copied-debian-libs.sh`
   records, for each library the build copies out of a Debian package, the
   package, version and source package, and copies the package's copyright file
   into the image; the `dpkg-copied` component claims them, fails on a missing
   copyright file, and puts the Debian sources into the `-source` image.
4. **rclone is built from its release's module source** (`go install
   github.com/rclone/rclone@$RCLONE_VERSION`, which checks every module against
   the checksum database); `build-config.env` pins `RCLONE_VERSION` in place of
   the vendor image.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| `google/go-licenses` for the module texts | An existing tool | A new pinned Go tool in every build; it reads the module graph of the source tree, not the binary, and does not handle a vendor program (rclone) or the source of copyleft modules | The binary's own build information is the exact list; the record and gate already exist |
| An LGPL-only FFmpeg (no `--enable-gpl`, so no x264 / x265) | Lighter obligations | The node worker loses its H.264 / HEVC software encoders, which vmaf-tune and the encode lanes use | Publishing the GPL source meets the licence and keeps the encoders |
| Keep copying the official rclone binary and publish the release source | No build change | `vcs.modified=true`: the binary was built from a modified tree, so the published source would not correspond to it (LGPL-3.0 4(d), ADR-1503 rule 5) | A source build is the only exact source |
| Drop rclone from the node image | No rclone obligations | `pkg/storage` needs it for remote inputs (ADR-0719) | Building it costs about two minutes |
| Install the FFmpeg dependencies with `dpkg` in a Debian-slim runtime | Native dpkg records | A larger image with a shell and package manager, against the distroless policy (ADR-0698, ADR-0815) | A small record next to the copied files keeps distroless |

## Consequences

- **Positive**: the Go images and the node image carry the texts and NOTICE
  files of everything they link, a redistributable FFmpeg, and source images
  whose content is verified against the binaries (module `h1:` sums, the
  compiled FFmpeg tree, the Debian package versions).
- **Negative**: the node `-source` image is about 640 MB (Debian sources 244 MB,
  rclone's and the copyleft modules' zips 376 MB, FFmpeg 17 MB); the builds need
  network access to the module proxy; a module whose licence file has an
  unusual name or text needs a `go_module_licences` entry.
- **Neutral / follow-ups**: Renovate tracked the rclone image digest; it now
  needs a version rule for `RCLONE_VERSION`. The unpublished node variants
  (`node-cuda`, `node-rocm`, `node-sycl`) and `Dockerfile.controller` gain the
  gate when they are published.

## References

- `Q` (popup 2026-10-04): "Audit now, then fix (Recommended)"; standing
  condition (paraphrased): no licence may be broken.
- [ADR-1513](1513-production-artifact-licensing.md), [ADR-1503](1503-tester-artifact-licensing.md),
  [ADR-0719](0719-vmafx-node-rclone-integration.md), [ADR-0717](0717-vmafx-node-ffmpeg-latest.md),
  [ADR-0815](0815-operator-node-distroless-dockerfiles.md).
- FFmpeg n9.0.2 `configure` (read 2026-10-04): `--enable-nonfree ... the resulting
  libs and binaries will be unredistributable`; `EXTERNAL_LIBRARY_NONFREE_LIST`
  (decklink, libfdk_aac, libmpeghdec), `HWACCEL_LIBRARY_NONFREE_LIST` (cuda_nvcc,
  cuda_sdk).
- Go: `debug/buildinfo` format (`.go.buildinfo`, inline strings since Go 1.18) and
  `golang.org/x/mod/sumdb/dirhash` Hash1, checked against `go mod download -json`
  sums on 2026-10-04.
- MPL-2.0 3.2; LGPL-3.0 4(d); GPL-3.0 6; Apache-2.0 4(a), 4(d).
