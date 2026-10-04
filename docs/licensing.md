# Licensing

VMAFx is a fork of Netflix's libvmaf, and its files carry different licences.
Each file's `SPDX-License-Identifier` header decides which one applies to it
([ADR-1250](adr/1250-eupl-fork-relicense.md)):

- files the fork wrote are under the **EUPL-1.2**;
- files inherited, ported or translated from Netflix keep Netflix's
  **BSD-2-Clause-Patent** licence and copyright notice;
- code from other projects keeps its own terms, named in the file: IQA
  (BSD-3-Clause), Xiph and dav1d (BSD-2-Clause), x264's assembly macros (ISC),
  the CIEDE2000 maths (MIT), libsvm and libjxl (BSD-3-Clause).

Because `libvmaf` links fork code and Netflix code into one library, a modified
`libvmaf` you distribute has to come with its source under the EUPL-1.2.

The licence texts are in the repository:
[`LICENSE`](https://github.com/VMAFx/vmafx/blob/master/LICENSE) (BSD-2-Clause-Patent)
and [`LICENSES/`](https://github.com/VMAFx/vmafx/tree/master/LICENSES) (every
other licence a file names). Files without a header of their own, such as model
data, are covered by
[`REUSE.toml`](https://github.com/VMAFx/vmafx/blob/master/REUSE.toml).

## Models

The VMAF models (`model/*.json`, `*.pkl`) are Netflix's (BSD-2-Clause-Patent).
The BRISQUE model is the LIVE laboratory's release, used under its notice as
written ([ADR-1507](adr/1507-brisque-live-notice-terms.md)): a publication that
reports research using it acknowledges LIVE and CPS at UT Austin and cites the
two works the notice names. The tiny-AI models list their licence and upstream
in [`model/tiny/registry.json`](https://github.com/VMAFx/vmafx/blob/master/model/tiny/registry.json)
and in their model cards ([model registry](ai/model-registry.md)). Three of them
carry upstream weights and keep their authors' terms: FastDVDnet (MIT, Matias
Tassano), TransNet V2 (MIT, Tomáš Souček) and LPIPS-SqueezeNet (BSD-2-Clause,
Zhang et al., on torchvision's BSD-3-Clause SqueezeNet features). The models the
fork trained itself are BSD-2-Clause-Patent; their cards name the data they were
trained on and that data's terms.

## What the published packages carry

Every published container image and package carries the notices and licence
texts of everything in it, and publishes the source its copyleft parts require
next to it ([ADR-1513](adr/1513-production-artifact-licensing.md),
[ADR-1503](adr/1503-tester-artifact-licensing.md)):

| Package | Notices and licence texts | Corresponding source |
| --- | --- | --- |
| `ghcr.io/vmafx/vmafx:<tag>` (CPU) | `/usr/local/share/vmafx/licenses/` in the image | `ghcr.io/vmafx/vmafx:<tag>-source` |
| `ghcr.io/vmafx/vmafx:<tag>-server` | `/usr/local/share/vmafx/licenses/` | `ghcr.io/vmafx/vmafx:<tag>-server-source` |
| `ghcr.io/vmafx/vmafx-operator:<tag>`, `vmafx-server:<tag>` | `/usr/local/share/vmafx/licenses/` (Go modules under `go/`) | `<image>:<tag>-source` |
| `ghcr.io/vmafx/vmafx-node:<tag>` | `/usr/local/share/vmafx/licenses/`; FFmpeg's and SVT-AV1's files under `/usr/local/share/vmafx/ffmpeg/` and `svt-av1/`; the copied libraries' copyright files under `copied-packages/` | `ghcr.io/vmafx/vmafx-node:<tag>-source` (FFmpeg as built with its configure line, Debian sources, Go module zips) |
| Tester images and bundles | `/opt/vmafx/licenses/`, `licenses/` in a bundle | `<image>-source` |
| `vmaf-mcp` on PyPI | the wheel's `licenses/` directory (EUPL-1.2) | the sdist |
| GitHub release files (`libvmaf.so*`, `vmaf`) | `THIRD_PARTY_NOTICES.txt` and `licenses.tar.gz` next to them on the release | the release's source archives |
| `models.tar.gz` (release) | `licenses/` inside the archive | the release's source archives |

`THIRD_PARTY_NOTICES.txt` in that directory names every component, its licence,
copyright lines and source; `texts/` holds the licence texts. The CPU image has
no shell, so copy the directory out to read it:

```bash
cid=$(docker create ghcr.io/vmafx/vmafx:<tag>)
docker cp "$cid:/usr/local/share/vmafx/licenses" ./vmafx-licenses
docker rm "$cid"
less ./vmafx-licenses/THIRD_PARTY_NOTICES.txt
```

The source image holds only files: pull it and copy `/sources` out the same way
(`SOURCES.txt` is its index). Each image also carries an attested SPDX SBOM;
[docker-production.md](development/docker-production.md#verifying-image-provenance)
shows how to verify it.

The FFmpeg in the node image is built with `--enable-gpl --enable-version3`
and is distributed under the GNU GPL version 3 or later; the Go programs list
every module they link with its licence in the `[go]` section of the notices
([ADR-1514](adr/1514-go-and-node-image-licensing.md)).

The GPU images gain the same treatment in the change that follows; the
releases up to 1.0.0-rc.2 lack it
([Research-2140](research/2140-production-artifact-licence-audit.md)).
