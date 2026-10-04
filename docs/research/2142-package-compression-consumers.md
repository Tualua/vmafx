<!-- markdownlint-disable MD013 MD060 -->
# Research-2142: Package compression and what each consumer opens

- **Status**: Active
- **Workstream**: [ADR-1591](../adr/1591-package-compression.md), [ADR-1594](../adr/1594-zstd-images-zopfli-zips.md)
- **Last updated**: 2026-10-04

## Question

Which compression can each published archive and image use so that every consumer its
guide names still opens it, how much does each candidate save on the real published
inputs, and what does it cost to produce?

## Sources

Consumer versions, read 2026-10-04:

- Docker Engine 23.0.0 (2023-02-01): "Add support for pulling `zstd` compressed layers"
  ([release notes](https://docs.docker.com/engine/release-notes/23.0/)).
- Docker Desktop: 4.18.0 bundles Engine 20.10.24, 4.19.0 (2023-04-27) is the first with
  Engine 23.0; 4.44.0 (2025-08-07) "Fixed an issue pulling images with zstd differential
  layers when the containerd image store is enabled"
  ([release notes](https://docs.docker.com/desktop/release-notes/)).
- Docker manifests (v2 schema 2) with zstd layers fail to pull; OCI manifests work
  ([docker/cli#5011](https://github.com/docker/cli/issues/5011)).
- containerd 1.5.0 (2021-05-03): "Add support for layers compressed with zstd"
  ([release](https://github.com/containerd/containerd/releases/tag/v1.5.0)).
- Kubernetes 1.26 requires containerd 1.6.0 or later
  ([release blog](https://kubernetes.io/blog/2022/12/09/kubernetes-v1-26-release/));
  the chart's floor is Kubernetes 1.26 (`docs/development/k8s-deployment.md`).
- containers/image (Podman, CRI-O, skopeo) reads zstd since
  [containers/image#639](https://github.com/containers/image/pull/639) (merged 2019-08-21).
- Docker Engine 20.10 reached end of life on 2023-12-10
  ([endoflife.date](https://endoflife.date/docker-engine)), but Debian 12 still ships
  `docker.io` 20.10.24+dfsg1-1+deb12u1 (`https://sources.debian.org/api/src/docker.io/`);
  Ubuntu 22.04 and 24.04 ship 29.1.3 in `-updates` (Launchpad `getPublishedBinaries`),
  Debian 13 ships 26.1.5.
- Debian 13 `Packages`: `gzip` is Essential, `xz-utils` Priority standard, `zstd`
  optional (`deb.debian.org/debian/dists/trixie/main/binary-amd64/Packages.xz`).
- macOS: the bundle targets macOS 14 (`MACOSX_DEPLOYMENT_TARGET=14.0` in
  `scripts/ci/build-macos-tester-bundle.sh`). Apple's `distribution-macOS` tags
  `macos-140` and `macos-150` pin `libarchive-121` (libarchive 3.5.3) and
  `libarchive-138`; both `config.h` files define `HAVE_LZMA_H` and leave
  `HAVE_LIBZSTD` and `HAVE_ZSTD_H` undefined, and the Xcode project links `bsdtar`
  against `lzma.tbd` ([apple-oss-distributions/libarchive](https://github.com/apple-oss-distributions/libarchive)).
  In `libarchive-121` the xz writer defaults to one thread and
  `HAVE_LZMA_STREAM_ENCODER_MT` is undefined (`archive_write_add_filter_xz.c`).
  Archive Utility opens `.xz`/`.txz` ([ctrl.blog](https://www.ctrl.blog/entry/archive-utility-xz),
  [list for 10.15.6](https://anderegg.ca/2020/08/14/what-file-types-does-apples-archive-utility-open),
  no `.zst`). `bsdtar(1)`: `-z` and `-J` are ignored when extracting; the
  compression is recognised automatically.
- Windows: the guide's floor is Windows 10 2004 (`docs/usage/tester-image.md`).
  `Expand-Archive` reads through .NET's `ZipArchive`, whose
  `IsOpenableInitialVerifications` accepts Stored, Deflate and Deflate64 only
  ([ZipArchiveEntry.cs](https://github.com/dotnet/runtime/blob/main/src/libraries/System.IO.Compression/src/System/IO/Compression/ZipArchiveEntry.cs)).
  CPython's `zipfile`, which the workflow's verify job unpacks with, has no Deflate64
  (`zipfile._check_compression(9)` raises `NotImplementedError`). No Microsoft page
  lists the methods Explorer's "Extract All" reads; Deflate is the method Explorer
  writes, so it is the one method every documented Windows tool is known to read.
- CPython 3.14's Windows binaries use zlib-ng
  ([What's New](https://docs.python.org/3.14/whatsnew/3.14.html)), as does the
  workstation's CPython 3.14.7 (zlib-ng 2.3.3), so the zip measurements below use the
  runner's zlib family.
- BuildKit (`moby/buildkit` master): `util/compression/gzip.go` writes with Go's
  `compress/gzip` at the configured level (default `gzip.DefaultCompression`) and its
  `NeedsConversion` is false for any gzip blob, so neither `compression-level` nor
  `force-compression` re-compresses a gzip layer; `cache/remotecache/gha/gha.go` fixes
  the GitHub Actions cache at `compression.New(compression.Default)`; zstd uses
  klauspost/compress, levels 9 to 22 map to its best level, about zstd 11
  ([exporter docs](https://docs.docker.com/build/exporters/#compression)).

## Findings

Inputs: tester release `tester-20261004-4d3792b3` (macOS), Windows workflow run
37190002031 (commit `8127730de`), release `v1.0.0-rc.2`, and the published GHCR images.
Times are wall clock on a 32-core host at load average 20 to 100, so only their ratios
carry over.

### macOS bundle (tar payload 185,152,000 bytes)

| Method | Bytes | Time |
| :--- | ---: | ---: |
| published (bsdtar `-czf`, gzip level 6) | 69,797,228 | |
| `gzip -9n` | 69,876,210 | 12.4 s |
| `zstd -19 --long=27` | 29,583,353 | 47.8 s |
| `zstd --ultra -22 --long=27` | 29,524,932 | 98.1 s |
| `xz -9 -T1` | 26,589,872 | 52.0 s |
| `bsdtar --options xz:compression-level=9 -cJf` (chosen) | 26,583,356 | 56.8 s |

The xz archive is byte-identical across two runs, holds one xz block, unpacks in 1 s,
and the unpacked tree equals the published one (`diff -r`). Level 9's 64 MiB window
sees the previous frames of the raw test videos, which gzip's 32 KiB window cannot.

### Windows zips

`writestr(ZipInfo, data)` ignores the `ZipFile`'s `compresslevel`: the script's own code
reproduces the zlib level 6 archive byte for byte (`master-code.zip` = `level6.zip`).

| Zip | Published | Level 6 (local) | Level 9 | zopfli, raw streams vs zlib 9 |
| :--- | ---: | ---: | ---: | :--- |
| x64 | 46,482,041 | 46,585,778 | 45,602,846 (3.4 s) | -3.72 %, 289 CPU s |
| arm64 | 40,883,647 | 40,978,592 | 40,131,773 (2.7 s) | -3.70 %, 252 CPU s |
| x64 CUDA | 350,615,890 | 351,365,970 | 343,773,549 (34.6 s) | -4.19 %, 3,214 CPU s |

7-Zip 26.03 Deflate `-mx=9` gives 43,784,707 bytes for x64 (40 s, one thread),
`-mfb=257 -mpass=15` 43,745,258 (145 s), Deflate64 42,903,497. Level 9 is
deterministic (two runs, same bytes) and reads back in the same time as level 6.

### git-archive source tarballs

`git archive --format=tar.gz` of this repository's `core/` and `scripts/`: 5,319,106
bytes at git's default level, 5,241,283 with `-9` (-1.5 %).

### Release tarballs (`models.tar.gz`, tar 51,957,760 bytes)

| Method | Bytes |
| :--- | ---: |
| published `gzip -n` (level 6; `gzip -6n` reproduces it exactly) | 42,249,786 |
| `gzip -9n` (chosen) | 42,202,744 |
| `zstd -19 --long=27` | 41,783,286 |
| `xz -9 -T1` | 41,406,948 |

The models are mostly ONNX and pickle floats; no method gains more than 2 %.

### Images (amd64, sum of layer sizes in bytes)

Each published gzip layer, decompressed and re-encoded with Go's encoders (Go 1.27
`compress/gzip`, klauspost/compress 1.20.1 zstd). The zstd figures equal BuildKit's:
the operator image re-exported through BuildKit at `compression=zstd,compression-level=3`
gives the same 24,624,226 bytes.

| Image | Published | zstd 3 | zstd best |
| :--- | ---: | ---: | ---: |
| `vmafx-operator` | 26,597,081 | 24,624,226 | 22,073,417 (-17.0 %) |
| `vmafx` (CPU CLI) | 55,664,004 | 55,820,033 | 51,499,236 (-7.5 %) |
| `vmafx-server` | 80,692,137 | 80,827,387 | 73,950,193 (-8.4 %) |
| `-cuda13` | 100,549,152 | 96,084,718 | 87,781,612 (-12.7 %) |
| `vmafx-node` | 153,700,838 | 152,774,921 | 138,824,071 (-9.7 %) |
| `-tester` | 267,776,859 | 229,781,834 | 200,067,558 (-25.3 %) |
| `-server` | 283,311,726 | 266,283,042 | 234,619,966 (-17.2 %) |
| `-tester-cuda` | 493,955,769 | 425,742,053 | 385,455,753 (-22.0 %) |
| `-tester-source` | 690,503,022 | 690,474,747 | 690,318,003 (0.0 %) |
| `-tester-sycl` | 935,834,881 | 917,603,114 | 884,061,331 (-5.5 %) |
| `-oneapi2025` | 1,424,426,946 | 1,228,663,361 | 1,073,310,596 (-24.6 %) |
| `-rocm10` | 8,309,870,053 | 7,410,415,655 | 7,064,598,170 (-15.0 %) |
| `vmafx-dev-mcp` | 17,025,909,842 | 15,231,802,909 | 13,986,415,021 (-17.9 %) |

gzip at level 9 against BuildKit's default level, measured with BuildKit itself: a
fresh builder (`moby/buildkit:buildx-stable-1`) per setting flattens the image into one
new layer (`FROM scratch`, `COPY --from`) and exports it as OCI. The uncompressed layer
digests (`rootfs.diff_ids`) are identical at every setting.

| Image, flattened | gzip default | gzip 9 | zstd 3 | zstd best |
| :--- | ---: | ---: | ---: | ---: |
| `vmafx-operator` | 26,606,368 | 26,498,835 (-0.4 %) | 24,529,210 | 21,945,660 (-17.5 %) |
| `-tester` | 264,907,857 | 264,175,167 (-0.3 %) | 226,670,199 | 197,012,637 (-25.6 %) |

On the tester the export took 46 s at the default level and 77 s at level 9 under load. Go's encoders, one thread, on the macOS tar: gzip default
25.7 MB/s, gzip 9 5.4 MB/s, zstd 3 192 MB/s, zstd best 5.7 MB/s.

### After the maintainer's choice (ADR-1594)

The maintainer chose zstd for every image (with a Docker 23.0 floor), zopfli for the
zips and zstd for the dev image. Checked before switching:

**What `force-compression` does with zstd.** `util/compression/zstd.go`:
`zstdType.NeedsConversion` is true for every layer type that is not zstd, so with
`force-compression=true` BuildKit converts gzip base-image layers and the gzip layers
the GitHub Actions cache returns. Exporting the operator image through a fresh
`docker-container` builder with
`compression=zstd,compression-level=22,force-compression=true,oci-mediatypes=true`
gave 14 of 14 layers `application/vnd.oci.image.layer.v1.tar+zstd` and 22,073,417 bytes,
exactly the Go encoders' figure in the table above. Without `oci-mediatypes`,
BuildKit's `toDockerLayerType` maps zstd to `application/vnd.docker.image.rootfs.diff.tar.zstd`
(`util/compression/compression.go`), the media type docker/cli#5011 fails on.

**Who pulls it.** The zstd operator image, pushed to a local `registry:2` and pulled by
`docker:<v>-dind` daemons (vfs storage driver, the classic image store):

| Runtime | Pull | Run `--version` | `docker load` of the `type=docker` export |
| :--- | :--- | :--- | :--- |
| Docker 20.10.24 (Debian 12's `docker.io` version) | fails: `failed to register layer: ApplyLayer exit status 1 stdout:  stderr: archive/tar: invalid tar header` | `docker: failed to register layer: ... invalid tar header.` | not tried |
| Docker 23.0.6 | ok | `v1.0.0-rc.2` | ok |
| Docker 28.5.2 | ok | `v1.0.0-rc.2` | ok |
| Docker 29.8.2, containerd image store (this host) | ok | `v1.0.0-rc.2` | |
| Podman 6.1.3 | ok | `v1.0.0-rc.2` | |

Syft v1.51.1 scanning the zstd and the gzip copy from the registry finds the same 186
packages; the SPDX documents differ only in the source version (the tag). The
publishing workflows pull with the hosted runners' Docker (28 or later) and attest with
the same Syft.

**Encode time.** Go's encoders (the BuildKit ones) on the first 1.5 GB of the dev
container's largest layer (4.72 GB gzip, about 11 GB raw), one stream, load average
about 3:

| Encoder | Output | Throughput |
| :--- | ---: | ---: |
| gzip default (today's dev image) | 434,922,434 | 185 MB/s |
| zstd 3 | 375,956,834 | 748 MB/s |
| zstd 8 | 351,758,617 | 399 MB/s |
| zstd 22 (best) | 328,091,166 (-24.6 %) | 64 MB/s |

The whole dev image (amd64, 52 layers, 41.0 GB raw) is 17,025,909,842 bytes as
published and 13,986,415,021 with zstd best (-17.9 %; zstd 3: 15,231,802,909). At
64 MB/s the largest layer takes about 3 minutes to encode against 1 minute today, and
the four largest layers (4.72, 4.25, 4.16 and 2.52 GB gzip) encode in parallel on the
runner's four cores; the push is 3 GB smaller. The dev job used 57 of its 90 minutes on
its last successful run (37088932576); the estimate is 58 to 62 minutes.

**zopfli on the real zips**, through the builder's own `pack()` (32 threads):

| Zip | zlib 9 | zopfli | CPU |
| :--- | ---: | ---: | ---: |
| arm64 | 40,131,773 | 38,655,329 (-3.68 %) | 347 s |
| x64 | 45,602,846 | 43,911,960 (-3.71 %) | 357 s |
| x64 CUDA | 343,773,549 | 329,387,580 (-4.18 %) | 3,215 s |

On a 4-vCPU runner the CUDA zip's 3,215 CPU seconds are about 14 minutes; its job took
10 of its 150 minutes (run 37190002031). Every zopfli zip passes `unzip -t`, `7z t`,
`bsdtar -t` and `zipfile.testzip()`, and .NET 9's `ZipFile.ExtractToDirectory` (what
`Expand-Archive` uses) unpacks each to a tree equal to the published one; on those trees
`licensing.py check` (artifacts `windows-zip`, `windows-cuda-zip`) exits 0 and the x64
SPDX document is unchanged. 50 iterations instead of 15 make a 25 MB sample of the x64
zip 0.09 % smaller for 2.9 times the CPU. zopfli releases the GIL: eight threads run
eight compressions about eight times faster than one.

## Alternatives explored

- **zstd layers for every image** (chosen by ADR-1594 with a Docker 23.0 floor): 5 to
  25 % smaller and faster to decompress, but a Docker Engine before 23.0 cannot pull them. Debian 12's own `docker.io` is 20.10.24,
  and every guide that names a consumer says "Docker" without a version
  (`docs/usage/tester-image.md`, `docs/usage/docker.md`, `docs/backends/operator.md`,
  `docs/usage/storage.md`, `docs/server/grpc.md`). Left to the maintainer: switching
  is one value per workflow once a floor is documented.
- **zopfli or 7-Zip Deflate for the zips** (zopfli chosen by ADR-1594, inside the
  builder's own writer): 3.7 to 4.2 % smaller than zlib 9 in the same format. `zipfile` cannot store pre-compressed data, so either would need a second
  zip writer or an external tool whose version moves with the runner image, plus a
  hash-locked build dependency on both Windows architectures and about 54 CPU minutes
  for the CUDA zip.
- **xz for the release tarballs**: 2 % on `models.tar.gz`, but `xz-utils` is not
  Essential on Debian, so a minimal system's `tar -xf` fails, and every release
  document and the licence record name `models.tar.gz`.
- **zstd for the macOS bundle**: 11 % larger than xz here, and Apple's libarchive is
  built without zstd.

## Open questions

- The BuildKit encode time of zstd 22 on the largest layers has not been timed on a
  hosted runner; the first runs after ADR-1594 show it (dev container: 57 of 90 minutes
  before; ROCm 10 production: 23 of 90; the Windows CUDA zip: 10 of 150).
- Whether Explorer's "Extract All" on Windows 11 reads zip methods beyond Deflate is not
  documented by Microsoft; it does not matter while Windows 10 is supported.

## Related

- [ADR-1591](../adr/1591-package-compression.md), [ADR-1594](../adr/1594-zstd-images-zopfli-zips.md), [ADR-1493](../adr/1493-macos-tester-bundle.md),
  [ADR-1515](../adr/1515-windows-tester-zip.md), [ADR-1503](../adr/1503-tester-artifact-licensing.md),
  [Artifact publishing policy](../development/publishing.md#compression).
