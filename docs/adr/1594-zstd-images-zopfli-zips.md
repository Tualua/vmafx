<!-- markdownlint-disable MD013 MD060 -->
# ADR-1594: zstd image layers with a Docker Engine 23.0 floor, and zopfli for the Windows zips

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: release, ci, docker, packaging, windows, testing, fork-local

## Context

[ADR-1591](1591-package-compression.md) left three packages short of the strongest
compression and named the maintainer's choice for each:

- **Image layers** stayed gzip at level 9. zstd is 5 to 25 % smaller, but a Docker
  Engine older than 23.0 cannot pull it, and the guides promised "any Docker host".
- **The Windows tester zips** stayed zlib level 9. zopfli writes the same Deflate
  format 3.7 to 4.2 % smaller, but Python's `zipfile` cannot store a stream it did not
  compress itself.
- **The dev container** (`vmafx-dev-mcp`) kept BuildKit's default level as an exception,
  for job time.

The maintainer chose all three (popup, 2026-10-04): zstd with Docker 23 as the floor,
zopfli for the zips, and zstd for the dev image too.

[Research-2142](../research/2142-package-compression-consumers.md) has the evidence:
what each runtime does with a zstd image, the BuildKit source on `force-compression`,
and the measured sizes and times.

## Decision

**Images.** Every workflow that pushes an image sets
`IMAGE_COMPRESSION: compression=zstd,compression-level=22,force-compression=true,oci-mediatypes=true`
and every push exports through `outputs:` ending in it. This covers the tester (four
images), production (five), operator, server, node, every `-source` image (through
`.github/actions/image-licence-artifacts`) and the dev container.

- Levels 9 to 22 map to klauspost's best encoder, about zstd 11.
- BuildKit's `zstdType.NeedsConversion` is true for every layer that is not zstd yet.
  With `force-compression`, the base-image layers and the layers imported from the
  GitHub Actions cache are therefore converted too. Without it they would stay gzip:
  the cache stores gzip whatever the image exporter writes.
- `oci-mediatypes=true` is required: under Docker manifest media types BuildKit labels
  the layers `application/vnd.docker.image.rootfs.diff.tar.zstd`, which Docker fails
  to pull ([docker/cli#5011](https://github.com/docker/cli/issues/5011)).
- The tester's local `load:` step is not published and keeps the shorthand.

The guides now state the minimum pull runtime:

- Docker Engine 23.0 or later
- Docker Desktop 4.19 or later
- containerd 1.5 or later (Kubernetes 1.26 already needs 1.6)
- Podman, CRI-O or skopeo

They also quote the error an older Docker prints, and `docs/usage/docker.md` holds the
table.

**Windows zips.** Every entry stays Deflate (method 8), encoded by zopfli 0.4.3 at its
default 15 iterations.

- zopfli is hash-locked in `requirements/locks/windows-tester-zip.txt` (HISS-11) and
  installed only by the build job; nothing of it ships.
- `build-windows-tester-bundle.py::pack()` runs zopfli on every processor (it releases
  the GIL) and writes the zip records itself. With zlib's stream in zopfli's place, the
  records are byte for byte what `zipfile` writes on Windows (create system 0,
  version 20, no data descriptor, no zip64); a test compares the two.
- The writer refuses anything that would need zip64.
- The `vmaf-rc1-report` bundle keeps zlib level 9: it runs on testers' machines with
  no dependencies.

**Dev container.** Same `IMAGE_COMPRESSION`; ADR-1591's exception is removed.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| zstd without `force-compression` | Base-image layers keep their upstream digests, so a host that has the base pulls less | Cached layers stay gzip (the GitHub Actions cache is gzip), so most of the image would not be zstd | The level must reach every layer |
| zstd level 3 (BuildKit default) | About 7.7 times faster to encode than level 22 | About 15 % larger than best on the tester image and the dev image's largest layer | The maintainer asked for the strongest; the job times stay inside their limits |
| Dual-compression images (gzip and zstd variants) | Old Docker would still pull | No registry-side selection: a client picks by platform, not by compression | Does not work |
| 7-Zip CLI for the zips | No Python dependency | A second tool whose version moves with the runner image, no hash pin, metadata of its own (directory entries, Windows attributes) | A pinned package inside the existing writer |
| Patch `zipfile._get_compressor` to use zopfli | Keeps `zipfile` writing | Private API of the standard library | A 60-line writer that a test holds to `zipfile`'s bytes |
| zopfli at 50 iterations | 0.09 % smaller on a 25 MB sample of the x64 zip | 2.9 times the CPU | 15 is zopfli's default |

## Consequences

- **Positive**:
  - Images shrink by 5.5 % (Intel GPU tester) to 25 % (tester, oneAPI), the dev image
    from 17.0 to 14.0 GB, and they decompress faster.
  - The Windows zips shrink by another 3.7 to 4.2 % (x64 CUDA 343.8 to 329.4 MB).
  - One policy for every image, with no exception left.
- **Negative**:
  - Docker Engine before 23.0 can no longer pull new images. That includes Debian 12's
    own `docker.io` (20.10.24), Docker Desktop before 4.19 and cri-dockerd on such an
    engine.
  - Base-image layers are re-encoded, so their digests no longer match upstream's and a
    host that already has the base image downloads them again.
  - Encoding at level 22 costs about 3 times the CPU of gzip's default on the dev
    container's largest layer.
  - zopfli adds about 3,200 CPU seconds to the CUDA zip build, about 14 minutes on a
    4-vCPU runner (the job takes 10 of its 150).
- **Neutral / follow-ups**:
  - Images published before this decision keep gzip layers and pull everywhere.
  - The first runs after landing show the job times: dev container (57 of 90 minutes
    before), ROCm 10 production (23 of 90) and the Windows CUDA zip.
  - A zopfli bump changes `requirements/locks/windows-tester-zip.in` and the
    rc1-tester dev pin together; `test_package_compression.py` checks they agree.

## Supply-chain impact

- **New dependencies**: `zopfli` 0.4.3 (build, Apache-2.0,
  <https://github.com/fonttools/py-zopfli>), wheels hash-locked for every platform in
  `requirements/locks/windows-tester-zip.txt`. The same pin is in the rc1-tester `dev`
  extra for its tests.
- **Build-time fetches**: one `pip install --require-hashes` in
  `windows-tester-bundle.yml`.
- **Sigstore-signable**: unchanged. Signatures, provenance and SBOM attestations name
  the manifest or zip digest, which now covers the new bytes.

## References

- req (2026-10-04, maintainer popup, as relayed by the coordinator): "zstd, require Docker 23+ (Recommended)"; "Yes, zopfli"; "zstd for the dev image (Recommended)".
- [ADR-1591](1591-package-compression.md) (superseded in part),
  [Research-2142](../research/2142-package-compression-consumers.md),
  [ADR-1515](1515-windows-tester-zip.md), [ADR-1503](1503-tester-artifact-licensing.md),
  [ADR-1305](1305-hash-locked-python-installs.md).
