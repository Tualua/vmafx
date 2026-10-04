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
| `ghcr.io/vmafx/vmafx:<tag>-cuda13`, `-rocm10`, `-oneapi2026` (and `-oneapi2025`) | `/usr/local/share/vmafx/licenses/`; the GPU vendors' texts under `nvidia/`, `rocm/` and `intel/` | `ghcr.io/vmafx/vmafx:<tag>-cuda13-source`, `-rocm10-source`, `-oneapi2026-source` |
| `ghcr.io/vmafx/vmafx-operator:<tag>`, `vmafx-server:<tag>` | `/usr/local/share/vmafx/licenses/` (Go modules under `go/`) | `<image>:<tag>-source` |
| `ghcr.io/vmafx/vmafx-node:<tag>` | `/usr/local/share/vmafx/licenses/`; FFmpeg's and SVT-AV1's files under `/usr/local/share/vmafx/ffmpeg/` and `svt-av1/`; the copied libraries' copyright files under `copied-packages/` | `ghcr.io/vmafx/vmafx-node:<tag>-source` (FFmpeg as built with its configure line, Debian sources, Go module zips) |
| Tester images and bundles | `/opt/vmafx/licenses/`, `licenses/` in a bundle | `<image>-source` |
| `vmaf-mcp` on PyPI | the `licenses/` directory of the wheel's and the sdist's metadata (EUPL-1.2, BSD-2-Clause-Patent) | the sdist |
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

The GPU images carry only the vendor files `vmaf` loads
([ADR-1517](adr/1517-gpu-image-licensing.md)):

- the CUDA image holds no NVIDIA library. The CUDA kernels inside `libvmaf`
  contain NVIDIA code from the CUDA Toolkit (its headers and the `libdevice`
  maths library), distributed under the CUDA Toolkit EULA; the notices pass its
  terms on, and the GPU driver (`libcuda`) comes from your host;
- the ROCm image holds the HIP runtime libraries and the system libraries ROCm
  bundles for them (two of them LGPL, with their source in the `-rocm10-source`
  image);
- the oneAPI image holds the Intel SYCL runtime files the compiler's licence
  lists as redistributable, provided for use with the VMAFx programs in the
  image (the Intel EULA forbids reverse engineering them), and the Intel GPU
  compute runtime.

## Python packages

Each Python package declares, as its PEP 639 `License-Expression`, every licence
its files carry, and ships each text in `LICENSES/` (installed under the
package's `.dist-info/licenses/`)
([ADR-1560](adr/1560-python-package-licence-union.md)). The files counted are
those in the package's sdist and wheel; for the `vmaf` harness that includes
every repository file its compiled ADM extension is built from.

| Package | Directory | `License-Expression` |
| --- | --- | --- |
| `vmaf` (Python harness) | `python/` | `BSD-2-Clause-Patent AND BSD-2-Clause AND BSD-3-Clause-Clear AND EUPL-1.2` |
| `vmaf-mcp` | `mcp-server/vmaf-mcp/` | `EUPL-1.2 AND BSD-2-Clause-Patent` |
| `vmaf-tune` | `tools/vmaf-tune/` | `EUPL-1.2 AND BSD-2-Clause-Patent` |
| `vmaf-train` | `ai/` | `EUPL-1.2 AND BSD-2-Clause-Patent` |
| `vmaf-dev-llm` | `dev-llm/` | `EUPL-1.2 AND BSD-2-Clause-Patent` |
| `vmaf-roi-score` | `tools/vmaf-roi-score/` | `EUPL-1.2 AND BSD-2-Clause-Patent` |
| `vmaf-ensemble-training-kit` | `tools/ensemble-training-kit/` | `EUPL-1.2 AND BSD-2-Clause-Patent` |

The expression describes the whole distribution, not each file: a module's own
header still says which licence applies to it. To read what an installed
package declares:

```bash
python3 -c "from importlib.metadata import metadata; print(metadata('vmaf-mcp')['License-Expression'])"
```

`python/test/setup_metadata_test.py` recomputes each union from the files and
fails when a package's metadata or texts disagree with them:

```bash
python3 -m pytest python/test/setup_metadata_test.py -k licence
```

The releases up to 1.0.0-rc.2 predate these rules
([Research-2140](research/2140-production-artifact-licence-audit.md)).
