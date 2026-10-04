<!-- markdownlint-disable MD013 MD060 -->
# ADR-1591: Publish every archive and image at the strongest compression its documented consumers open

- **Status**: Accepted (Superseded-in-part 2026-10-04 by [ADR-1594](1594-zstd-images-zopfli-zips.md) for the image layers, the Windows zip encoder and the dev container exception)
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: release, ci, docker, packaging, windows, macos, testing, fork-local

## Context

The maintainer set compression as a standard for every build and every published
artifact: as strong as practical, with nothing computed differently. Device code is a
separate decision ([ADR-1590](1590-device-code-compression.md)); this one covers the
packaging: the tester archives, the release tarballs, the source tarballs and the
container images.

The packages used whatever their tools default to. The macOS tester bundle was
`tar -czf` (gzip level 6). The release tarballs were `gzip -n` (level 6). BuildKit
writes gzip layers at Go's default level. The Windows tester zips claimed
`compresslevel=9` and carried level 6: `zipfile` applies a `ZipFile`'s level only to
entries it names itself, and the build writes every entry as a `ZipInfo`
(`T-WINDOWS-ZIP-COMPRESSLEVEL-IGNORED-2026-10-04`).

A stronger method is only an improvement if every consumer the guides name still opens
the package. The consumers differ per package: macOS 14's own `tar` and Archive Utility
for the bundle; Windows 10's `tar`, Explorer and `Expand-Archive` for the zips; any
Linux `tar` for the release tarballs; "any Docker host" (and Kubernetes 1.26 or later
for the chart) for the images. [Research-2142](../research/2142-package-compression-consumers.md)
records the consumer versions with their sources and measures every candidate on the
published rc.2 and tester inputs.

## Decision

Each published package uses the strongest compression all of its documented consumers
open, written deterministically:

- **macOS tester bundle**: tar + xz at level 9 (`--options xz:compression-level=9
  -cJf`, libarchive's single-threaded writer), named `.tar.xz`. Apple builds libarchive
  with liblzma and without zstd (macOS 14 and 15), so `tar -xf` and Archive Utility read
  it with nothing installed. 69.8 MB becomes 26.6 MB.
- **Windows tester zips**: Deflate at zlib level 9 on every entry (`ZIP_LEVEL`, passed
  to `writestr()`). Deflate is the one method every documented Windows tool reads;
  -2.1 % to -2.2 % per zip.
- **Release tarballs** (`models.tar.gz`, `licenses.tar.gz`): `gzip -9n`, names kept.
  gzip is the one compressor a minimal Debian `tar` extracts.
- **git-archive source tarballs** (`licensing.py fetch-sources`, the FFmpeg source of
  `docker/Dockerfile.node`): `git archive --format=tar.gz -9`.
- **Container images and their source images**: every exporter of the tester,
  production and operator/server/node workflows writes through `outputs:` with the
  workflow's `IMAGE_COMPRESSION`, `compression=gzip,compression-level=9`, including the
  tester's `load` step (the first exporter to create a layer's blob decides its level,
  and the GitHub Actions cache keeps that blob). The `push:` and `load:` shorthands are
  not used there. No `force-compression`: BuildKit never re-compresses a gzip layer, so
  the base-image layers keep their upstream digests.
- **The rc1-tester report bundle**: its zip passes level 9 per entry like the Windows
  zip; its tar.gz was already level 9.
- **Exception**: `ghcr.io/vmafx/vmafx-dev-mcp` (`dev-container-publish.yml`) keeps
  BuildKit's default level until 2026-12-31. It is 17 GB of gzip layers, off the release
  path, and its job uses 57 of its 90 minutes; level 9 encodes about five times slower.

`scripts/ci/tests/test_package_compression.py` holds the workflows and scripts to this
decision and expires the exception. `tools/rc1-tester/tests/test_windows_bundle.py`
and `test_bundle.py` compare each zip entry's deflate stream with zlib level 9 byte
for byte; `scripts/release/tests/test-build-native-release-artifacts.sh` reads the gzip
header's XFL byte. The table of every artifact is in
[Artifact publishing policy](../development/publishing.md#compression).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| zstd layers (best level) for every image | 5 to 25 % smaller images (tester -25 %, oneAPI -25 %, operator -17 %), faster decompression | Docker Engine before 23.0 cannot pull them; Debian 12's own `docker.io` is 20.10.24; every guide names "Docker" without a version | A documented consumer would fail to pull; a maintainer decision to set a Docker floor first (one value per workflow then) |
| zstd for the Kubernetes-only images only | Kubernetes 1.26 implies containerd 1.6 or CRI-O, both read zstd | Operator, server and node images all have documented `docker run` / `docker pull` lines | No image is Kubernetes-only in the guides |
| gzip level 9 with `force-compression=true` | Would look like it re-compresses base layers | BuildKit's `NeedsConversion` is false for gzip, so it changes nothing | No effect |
| Keep BuildKit's default level for images | No CI time | Ignores the standard; level 9 is free of consumer risk | The gain is small (0.3 to 0.4 %) but costs only encode time |
| zopfli or 7-Zip Deflate for the zips | 3.7 to 4.2 % smaller than zlib 9, same format | `zipfile` cannot store pre-compressed data: a second zip writer or an external tool whose version moves with the runner; a new hash-locked build dependency on two architectures; about 54 CPU minutes per CUDA zip | Cost out of proportion; left as an open question |
| Deflate64, LZMA or zstd entries in the zips | Smaller | CPython's `zipfile` (the verify job) has no Deflate64; .NET's `ZipArchive` (`Expand-Archive`) reads Stored, Deflate and Deflate64 only | A documented tool could not open them |
| zstd for the macOS bundle | Faster to write | 11 % larger than xz on this payload; Apple's libarchive has no zstd | xz is smaller and built in |
| xz for the release tarballs | 2 % smaller `models.tar.gz` | `xz-utils` is not Essential on Debian; renames assets the licence record and the docs name | gzip -9 keeps every consumer and every name |

## Consequences

- **Positive**: the macOS bundle download drops by 62 %; the Windows zips actually
  carry the level they claimed; every image layer a build creates is gzip level 9; one
  test fails when a publishing workflow loses the setting or a new workflow pushes an
  image outside the policy.
- **Negative**: the macOS asset changes its name from `.tar.gz` to `.tar.xz` (the
  guide's commands change with it); gzip level 9 encodes about five times slower than
  the default, which lengthens image pushes by the encode time of each new layer (most
  in the ROCm 10 and oneAPI production images); the tester's local build step encodes
  at level 9 too.
- **Neutral / follow-ups**: the first production release after this decision shows the
  ROCm 10 job's time against its 90-minute limit; the dev container's exception expires
  on 2026-12-31 and needs either level 9 with a longer limit or a renewed exception; a
  Docker Engine 23.0 floor in the guides would let every image move to zstd. Licence
  checks and SBOMs are unchanged: they read the unpacked tree or the image, whose files
  do not change.

## References

- req (2026-10-04, maintainer, paraphrased in brief): compress all builds as well as we can; that should be the standard.
- [Research-2142](../research/2142-package-compression-consumers.md) (consumer versions, measurements).
- [ADR-1590](1590-device-code-compression.md) (device code), [ADR-1493](1493-macos-tester-bundle.md), [ADR-1515](1515-windows-tester-zip.md),
  [ADR-1516](1516-windows-cuda-tester-zip.md), [ADR-1503](1503-tester-artifact-licensing.md),
  [ADR-1513](1513-production-artifact-licensing.md).
