<!-- markdownlint-disable MD013 MD060 -->
# Research-2140: Licence audit of the published production artifacts

Audit date 2026-10-04. Input to [ADR-1513](../adr/1513-production-artifact-licensing.md),
which applies the tester rules of [ADR-1503](../adr/1503-tester-artifact-licensing.md)
to the production artifacts. Every row comes from the published bytes: the
v1.0.0-rc.2 images were exported per platform with `crane export` (the 20.9 GB
ROCm image by streaming its layers), the release assets downloaded with
`gh release download`, the PyPI files read from the release, and the licence
terms from the texts listed under *Licence sources*. `licensing.py scan-build`
of the CPU release configuration gives the licences of the VMAFx binaries; the
Go modules are those of each binary's build information, classified from the
module's own `LICENSE` and `NOTICE` files.

Severity: **breaks** = a licence condition is unmet by what is published;
**weak** = arguable, or a pass-on term missing where the grant still holds;
**cosmetic** = metadata wrong, no condition unmet.

## Inventory

| Artifact | Where | Built by | Published versions |
|---|---|---|---|
| CPU CLI image | `ghcr.io/vmafx/vmafx:<tag>` (amd64+arm64), `latest` | `docker/Dockerfile.production` target `cli` | v1.0.0-rc.1, v1.0.0-rc.2 |
| MCP server image | `ghcr.io/vmafx/vmafx:<tag>-server` | `docker/Dockerfile.production` target `server` | rc.1, rc.2 |
| CUDA image | `ghcr.io/vmafx/vmafx:<tag>-cuda13` | `docker/Dockerfile.production-gpu` `final-cuda13` | rc.1, rc.2 |
| ROCm image | `ghcr.io/vmafx/vmafx:<tag>-rocm10` | `final-rocm10` | rc.1, rc.2 |
| oneAPI image | `ghcr.io/vmafx/vmafx:<tag>-oneapi2025` (master: also `-oneapi2026`) | `final-oneapi2026` (rc tags: `final-oneapi2025` on `intel/oneapi-runtime:2025.3.1-0-devel-ubuntu24.04`) | rc.1, rc.2 |
| Go server image | `ghcr.io/vmafx/vmafx-server:<tag>` | `Dockerfile.go-server` target `go-server` | rc.1, rc.2 |
| Operator image | `ghcr.io/vmafx/vmafx-operator:<tag>` | `docker/Dockerfile.operator` | rc.1, rc.2 |
| Node image | `ghcr.io/vmafx/vmafx-node:<tag>` | `docker/Dockerfile.node` target `node-cpu` | rc.1, rc.2 |
| Controller image | not published (no workflow builds `docker/Dockerfile.controller`) | — | — |
| Dev container | `ghcr.io/vmafx/vmafx-dev-mcp:master`, `:sha-*` — **private** package, 87 versions | `dev-container-publish.yml` (push to master touching `dev/`) | continuously, last 2026-10-03 |
| Native release assets | GitHub release v1.0.0-rc.1, rc.2: `libvmaf.so{,.3,.3.0.0}`, `vmaf`, `models.tar.gz`, SBOMs (SPDX+CycloneDX), signatures, provenance | `supply-chain.yml` `build-artifacts` / `attach-to-release` | rc.1, rc.2 |
| PyPI | `vmaf-mcp` 1.0.0rc1, 1.0.0rc2 (pure wheel + sdist), also release assets | `supply-chain.yml` `mcp-build` / `mcp-publish-pypi` | rc1, rc2 |
| Tester artifacts | `-tester` image, macOS bundle release `tester-20261003-c12763f3` | covered by ADR-1503 (follow-up there) | 2026-10-03 |
| Helm chart | not published (`helm-chart.yml` lints only) | — | — |
| Homebrew / conda / deb / other | none | — | — |
| FFmpeg | patch series in-tree; distributed as a **binary** only inside the node image | `docker/Dockerfile.node` | rc.1, rc.2 |
| Models | inside every image (`/usr/local/share/vmafx/model`) and `models.tar.gz` | — | rc.1, rc.2 |
| Docs site footer | `mkdocs.yml` `copyright: "BSD-2-Clause-Patent — Netflix / Lusoris"` | GitHub Pages | live |

The open release PR for 1.0.0-rc.3 would publish every artifact above again
with the same gaps unless the fixes land first.

## VMAFx's own code in every binary

`licensing.py scan-build` of the CPU release configuration (359 compiled files): `EUPL-1.2`, `BSD-2-Clause-Patent` (Netflix 2016-2026),
`BSD-3-Clause` (IQA / Tom Distler, libsvm / Chang & Lin, libjxl), `BSD-2-Clause`
(Xiph, Daala, dav1d / VideoLAN, Two Orioles), `ISC` (x264 `x86inc.asm`), `MIT`
(CIEDE2000 / Joshua Holmer, Stephen Mathieson), `Unlicense`,
`LicenseRef-LIVE-BRISQUE` (embedded model). BSD-2/3 clause 2, ISC and MIT require
the notice and licence in the documentation or other materials of a binary
distribution; the LIVE notice must appear "in its entirety in all copies".
EUPL-1.2 Art. 5 asks a distributor to provide or point to the source. No
published artifact carries any of these texts except `NOTICE-brisque` (inside
the model directory).

## Per artifact

### CPU CLI image (`:v1.0.0-rc.2`, distroless cc-debian13)

| Content | Licence | Obligation on redistribution | Met today |
|---|---|---|---|
| `libvmaf.so*`, `vmaf` | VMAFx set above | notices + texts; EUPL source pointer | no |
| models `/usr/local/share/vmafx/model` (Netflix JSON/pkl, tiny ONNX, BRISQUE) | BSD-2-Clause-Patent (Netflix), MIT (fastdvdnet, TransNetV2), BSD-2-Clause (LPIPS), LIVE notice, fork models | notices + texts | only `NOTICE-brisque` |
| 14 Debian packages via `var/lib/dpkg/status.d` (libc6 2.41-12+deb13u4, libgcc-s1/libstdc++6/libgomp1 14.2.0-19, libssl3t64 3.5.7, zlib1g, libzstd1, tzdata, base-files, ca-certificates, netbase, media-types) | glibc LGPL-2.1+, GCC runtime GPL-3+ WITH GCC-exception-3.1, OpenSSL Apache-2.0, … | copyright files (present); LGPL-2.1 §6 / GPL-3 §6: corresponding source at the same place | copyright yes; source **no** |
| OCI label `licenses=BSD-2-Clause-Patent` | — | — | wrong |
| SBOM | CycloneDX via `cosign attest` | (ADR-1503 R5: SPDX via `actions/attest`) | partial |

### MCP server image (`-server`, python:3.14-slim, Python 3.14.7)

| Content | Licence | Obligation | Met |
|---|---|---|---|
| VMAFx binaries + models | as above | notices | no |
| CPython in `/usr/local` | PSF-2.0 + incorporated software (`Doc/license.rst`: expat, libmpdec, HACL*, …) | notices of the incorporated software | `LICENSE.txt` only (no `Doc/license.rst`, no HACL*) |
| 73 dist-infos in `/venv` + system site | various; Cython, flatbuffers, onnxruntime keep no licence file in their dist-info; `vmaf-mcp` / `vmaf-tune` declare BSD-2-Clause-Patent, files are EUPL-1.2, no licence file | licence text per package | mostly; 5 without |
| grafted `libgfortran` (build IDs 04dbe32d…, 5bbe74eb…) and `libquadmath` (c1c5f8d6…, 549b4c82…) in numpy/scipy | GPL-3+ WITH GCC-exception, LGPL-2.1+ | corresponding source | **no** (same IDs the tester record already maps to SRPMs) |
| 87 Debian packages | 77 name GPL/LGPL | source at the same place | **no** |
| build tools left in the runtime venv (build, hatchling, Cython, setuptools, wheel) | permissive | notices | n/a (not needed at run time) |

### CUDA image (`-cuda13`, ubuntu:26.04)

| Content | Licence | Obligation | Met |
|---|---|---|---|
| `libvmaf.so*` with CUDA fatbins | VMAFx set + NVIDIA device code (CUDA headers inlined, `libdevice.10.bc` linked by nvcc) | CUDA EULA v13.4 (2026-01-26) 1.1.1(c), 1.1.2(a)(b)(e): distributable portions in object code inside an application with material additional functionality, accessed only by it, terms consistent with the EULA | VMAFx notices no; NVIDIA terms not passed on |
| `nv-codec-headers` code compiled into libvmaf | MIT | notice | no |
| `cuda-cudart-13-4` 13.4.92-1 (`libcudart.so.13.4.92`, Attachment A) | CUDA EULA (its dpkg copyright file is the v13.4 text) | 1.1.2(b) only accessed by our application; unmodified (2.3) | unmodified yes; **not loaded by libvmaf** (`readelf -d`: no NVIDIA NEEDED), so it is a vendor file in a general image that any program can load |
| `cuda-keyring` | NVIDIA apt key | copyright file | missing |
| `curl`, `gpgv` (installer leftovers) + 118 Ubuntu packages, 104 GPL/LGPL | GPL/LGPL | source at the same place (Ubuntu: Launchpad; no snapshot fallback) | **no** |

### ROCm image (`-rocm10`, the whole `rocm/dev-ubuntu-26.04:10.0.0-full`)

Measured by streaming the layers of `ghcr.io/vmafx/vmafx:v1.0.0-rc.2-rocm10`
(54,878 entries, 20.9 GB uncompressed, 518 dpkg packages).

| Content | Licence | Obligation | Met |
|---|---|---|---|
| `librocprof-trace-decoder.so.0.2.0` (package `amdrocm-profiler-base10.0`) | AMD Software End User License Agreement (`ROCm/rocprof-trace-decoder` `LICENSE`): §2 grants use and distribution of *Derivative Works* only; **§3.2: you may not "distribute, publish, display, sublicense, assign or otherwise transfer the Software"** | not redistributable | **shipped verbatim, no licence text in the image** |
| ROCgdb (`opt/rocm/core-10.0/bin/rocgdb*`, `amdrocm-debugger10.0`) | GPL-3.0-or-later (ROCm `NOTICES.txt`) | §6 corresponding source | **no source** |
| compilers and SDK: `hipcc`, `amdclang`, `clang`, `ld.lld`, 66 tools in `bin/`, BLAS / FFT / RAND / SOLVER / SPARSE / DNN (MIOpen) / RCCL / CK for 28 gfx targets, devel headers | MIT / NCSA / Apache-2.0 WITH LLVM-exception / custom (hipCUB, RCCL) | notices per component | partly (`share/doc/*/LICENSE*`, `NOTICES.txt`); the `amdrocm-*` packages have **no** `/usr/share/doc/<pkg>/copyright` |
| `libamdhip64`, `libhsa-runtime64`, `libamd_comgr`, `rocm_sysdeps` (libelf, libnuma LGPL) — what libvmaf actually loads | MIT / NCSA / Apache-2.0 WITH LLVM-exception / LGPL | notices; LGPL source | no notices assembled, no source |
| VMAFx binaries + models | VMAFx set | notices | no |
| Ubuntu 26.04 packages (GPL/LGPL) | GPL/LGPL | source at the same place | **no** |

### oneAPI image (`-oneapi2025`, rc tags: `intel/oneapi-runtime:2025.3.1-0-devel-ubuntu24.04`)

| Content | Licence | Obligation | Met |
|---|---|---|---|
| Intel runtime set `/opt/intel/oneapi/redist` (417 files, 3.1 GB: SYCL, compiler runtime, MKL, IPP, MPI, CCL, DAL, DNNL, Fortran, OpenCL CPU device `libintelocl`, `libomptarget`, `icx-lto.so`) | compiler runtime: Intel EULA for Developer Tools (Aug 2024) Redistributables; MKL/IPP/TBB/MPI/CCL/DAL/TCM: Intel Simplified Software License (Oct 2022); DNNL, ippcp: Apache-2.0 | EULA 2.1.D(1) only as part of our product; (2) executable code only and **under a licence agreement that prohibits reverse engineering**; (4)(iii) no Intel marks. ISSL: notice and terms reproduced, no reverse engineering | texts present inside `/opt/intel`; **no pass-on of 2.1.D(2) terms**; most of the set is not used by libvmaf |
| 30 `intel-oneapi-*` packages | — | copyright file | none in `/usr/share/doc` (texts under `/opt/intel`) |
| gcc-13/g++/cmake/make/binutils/libc6-dev (devel base) | GPL | source | **no** |
| Ubuntu 24.04 packages (324 total) | GPL/LGPL | source at the same place | **no** |
| Level Zero GPU driver, IGC, gmmlib, OpenCL ICD | MIT / Apache-2.0 WITH LLVM-exception | notices | packaged copyright files |

Master recipe (ADR-1368) installs `intel-oneapi-runtime-dpcpp-cpp intel-oneapi-umf`
plus the NEO runtime set on Debian 13: smaller, but still the meta-package set
ADR-1505 measured at 1.1 GB with OpenCL CPU device, OpenMP offload and LTO
plugin, and no pass-on of 2.1.D(2).

### Go server, operator images (distroless cc / static)

| Content | Licence | Obligation | Met |
|---|---|---|---|
| `vmafx-server` (183 modules), `vmafx-operator` (177) | modules: 163 Apache-2.0 (31 with `NOTICE`), 140 MIT, 77 BSD, 13 MPL-2.0 (riverqueue/river ×5, hashicorp go-cleanhttp, go-retryablehttp), ISC, CC0; own code EUPL-1.2 (golusoris EUPL-1.2) | Apache-2.0 §4(a)(d) licence + NOTICE; MIT/BSD/ISC notice; MPL-2.0 §3.2(a) tell recipients how to get the Source Code Form | **no** texts, no NOTICE, no MPL source information |
| go-server also: libvmaf + vmaf + models | VMAFx set | notices | no |
| distroless Debian packages (go-server: same 14 as CPU; operator static: base-files, netbase, tzdata, ca-certificates, media-types) | glibc LGPL etc. | source | **no** |

### Node image (`vmafx-node`, distroless cc)

| Content | Licence | Obligation | Met |
|---|---|---|---|
| `ffmpeg`, `ffprobe`, `libav*`, `libsw*` (n9.0.2 + fork patches) | configured `--enable-gpl --enable-version3 --enable-nonfree`: `ffmpeg -L` prints "This version of ffmpeg has nonfree parts compiled in. Therefore it is not legally redistributable." (no nonfree component is actually enabled: decklink, libfdk_aac, libmpeghdec, cuda_nvcc, cuda_sdk absent from the configure line; the build is otherwise GPL-3.0-or-later) | do not redistribute a nonfree build; GPL-3 §4, §6: licence text, corresponding source (FFmpeg + patches + configure line + linked GPL libraries) | **no** — self-declared unredistributable, no text, no source |
| 40 libraries copied with `cp -L` out of Debian packages into `/usr/local/lib`: libx264 (GPL-2+), libx265 (GPL-2+), libmp3lame, libglib-2.0, libfribidi, libnuma, libgraphite2 (LGPL), libfreetype (FTL credit clause / GPL-2), libvpx, libopus, libogg, libvorbis, libdav1d (BSD), libass (ISC), libharfbuzz, libX11/xcb/Xau/Xdmcp/Xext/Xfixes, libva, libvdpau, libdrm, libexpat (MIT), libpcre2, libbz2, libz, libbrotli, libunibreak, libatomic (GPL-3+ exception) | per library | copyright file, notices, source for (L)GPL | **none**: no dpkg record, no copyright file, no source |
| `libSvtAv1Enc.so.4` (SVT-AV1 v4.2.0 built from source) | BSD-3-Clause-Clear + AOM Patent License 1.0 | notice + licence | no |
| `rclone` 1.75.1 (from `rclone/rclone`) | MIT; 244 modules incl. LGPL-3.0 `cloudsoda/sddl`, MPL-2.0 hashicorp ×6 and anacrolix ×2 | MIT notices; LGPL-3 §4(d): corresponding source / relinkable form; MPL §3.2 | **no** |
| `vmafx-node` (179 modules) | as Go images | as Go images | **no** |
| `libvmaf.so.3.0.0.p/` (Meson object directory caught by `cp -a libvmaf.so*`) | EUPL/BSD objects | — | junk |

### Native release assets (v1.0.0-rc.1, rc.2)

| Content | Licence | Obligation | Met |
|---|---|---|---|
| `libvmaf.so*`, `vmaf` | VMAFx set (above) | notices + texts in the documentation of the binary distribution | **no** licence text or notice among the assets |
| `models.tar.gz` (`model/`) | Netflix BSD-2-Clause-Patent, MIT, BSD-2-Clause, LIVE | notice + texts in every copy | only `NOTICE-brisque` |
| SBOMs | SPDX + CycloneDX of the native files, signed blobs | ADR-1503 R5: attested | signed, not `actions/attest`ed with `sbom-path` |
| EUPL Art. 5 source | GitHub's tag archive on the same release page | source pointer | yes (same page) |

### PyPI `vmaf-mcp` (and `vmaf-tune` in the server image)

| Content | Licence | Obligation | Met |
|---|---|---|---|
| `vmaf_mcp/*.py` | EUPL-1.2 headers (Copyright 2026 Lusoris) | EUPL Art. 5 notices; metadata | metadata `License-Expression: BSD-2-Clause-Patent` contradicts every file; no licence file in wheel or sdist |

### Models

| Model | Licence recorded | Finding |
|---|---|---|
| Netflix `vmaf_*.json/pkl`, `other_models/*` | BSD-2-Clause-Patent (REUSE `model/**`) | needs the Netflix notice with each copy |
| `brisque_live.model` | LicenseRef-LIVE-BRISQUE (ADR-1507) | notice travels (`NOTICE-brisque`) |
| `tiny/fastdvdnet_pre.*` | MIT, Tassano (REUSE) | notice needed in copies |
| `tiny/lpips_sq.*` | REUSE: BSD-2-Clause-Patent "2026 Lusoris"; registry + card: BSD-2-Clause upstream (Zhang, Isola, Efros, Shechtman, Wang 2018) | **REUSE wrong**: computed notices would drop the upstream notice |
| `tiny/transnet_v2.*` | REUSE: BSD-2-Clause-Patent Lusoris; registry + card: MIT upstream (Copyright (c) 2020 Tomáš Souček) | **REUSE wrong** |
| `predictor_*.onnx`, `konvid_mos_head_v1.*`, `*_card.md` at `model/` root | REUSE `model/**`: "2016-2020 Netflix, Inc." | fork-trained, wrong holder |
| `fr_regressor_v1`, `vmaf_tiny_v1..v4` | BSD-2-Clause-Patent | trained on the Netflix Public Dataset, whose licence forbids redistribution; the card argues the weights are not a redistribution (legal reading) |
| `saliency_student_v1/v2` | BSD-2-Clause-Patent | trained on DUTS-TR, "free for academic and research purposes" |
| `nr_metric_v1`, `learned_filter_v1`, `vmaf_tiny_v2+` | BSD-2-Clause-Patent | cards state KoNViD-1k is "CC BY 4.0"; the database page (read 2026-10-04) says only "freely available to the research community"; its videos are YFCC100M CC clips of mixed licences |
| `mobilesal`, `dists_sq`, `smoke_*`, synthetic predictors | fork placeholders | fine |

### Dev container (`vmafx-dev-mcp`, private)

`libvmaf-build` stage: full CUDA toolkit (`install-cuda-toolkit.sh --mode=full`:
nvcc, headers, tools: not Attachment A), `intel-basekit` (compilers and
libraries; Intel EULA allows only Redistributables, 2.1.D), ROCm payload.
Private, org has one member: internal use (CUDA EULA 1.1.1(a), Intel EULA
2.1.A) holds. Making it public, or granting outside access, would distribute
non-distributable toolkit files.

### Docs site footer

`mkdocs.yml`: `copyright: "BSD-2-Clause-Patent — Netflix / Lusoris"`. Since
ADR-1250 fork-authored files are EUPL-1.2 and the per-file SPDX header decides.

## Ranked gap list

| # | Gap | Artifact | Obligation | Licence source | Severity |
|---|---|---|---|---|---|
| 1 | ROCm image redistributes `librocprof-trace-decoder.so` (binary-only, AMD Software EULA §3.2 forbids distributing the Software) | -rocm10 rc.1, rc.2 | not redistributable at all | `ROCm/rocprof-trace-decoder` `LICENSE`; ROCm 10.0.0 licensing page | breaks |
| 2 | FFmpeg configured `--enable-nonfree`; the binary declares itself "not legally redistributable" | vmafx-node rc.1, rc.2 | do not redistribute a nonfree build | FFmpeg n9.0.2 `configure` (line 102, 4840), `ffmpeg -L` of the published binary | breaks |
| 3 | ROCm image final stage is the whole 20.9 GB dev image: ROCgdb (GPL-3.0) without source, compilers, SDK libraries for 28 targets, `amdrocm-*` packages without copyright files | -rocm10 rc.1, rc.2 | GPL-3 §6; component notices | ROCm 10.0.0 licensing page, `NOTICES.txt` | breaks |
| 4 | GPL-3.0-or-later FFmpeg + GPL-2+ x264/x265 shipped without licence text or corresponding source | vmafx-node | GPL-3 §4, §6 | GPL-3.0 text | breaks |
| 5 | 40 Debian libraries copied out of their packages without copyright files, notices or source; SVT-AV1 without notice; rclone with LGPL-3.0 and MPL-2.0 modules, no notices, no source | vmafx-node | LGPL-2.1 §6, GPL-2 §3, FTL, BSD/MIT/ISC notices, BSD-3-Clause-Clear, LGPL-3 §4, MPL-2.0 §3.2 | each package's copyright; module LICENSE files | breaks |
| 6 | Go binaries: no licence texts, no Apache NOTICE files, no MPL source information | vmafx-server, -operator, -node | Apache-2.0 §4(a)(d), MIT/BSD/ISC, MPL-2.0 §3.2(a) | module LICENSE / NOTICE files | breaks |
| 7 | VMAFx binaries without their BSD/ISC/MIT notices and texts | every image and the native release assets | BSD-2-Clause-Patent cl. 2, BSD-3 cl. 2, BSD-2 cl. 2, ISC, MIT | `LICENSE`, `LICENSES/*` | breaks |
| 8 | GPL/LGPL Debian/Ubuntu object code with no corresponding source at the same place | every image | LGPL-2.1 §6, GPL-2 §3, GPL-3 §6 | `/usr/share/common-licenses` | breaks |
| 9 | Grafted libgfortran / libquadmath without source; CPython incorporated-software notices missing | -server | GPL-3 §6, LGPL-2.1 §6; expat/libmpdec/HACL* MIT/BSD | CPython `Doc/license.rst` | breaks |
| 10 | Intel runtime redistributed without the 2.1.D(2) pass-on terms; most of the set unused | -oneapi2025 / -oneapi2026 | Intel EULA for Developer Tools (Aug 2024) 2.1.D(1)(2) | EULA text in the image (`redist/share/doc/compiler/licensing/c/LICENSE`) | breaks |
| 11 | `models.tar.gz` without licence texts | release assets | MIT / BSD notices | REUSE.toml, upstream LICENSE files | breaks |
| 12 | REUSE.toml attributes upstream LPIPS / TransNetV2 weights and fork root models wrongly | models everywhere | MIT / BSD notice of the right holder | upstream LICENSE files | breaks (once notices are computed from it; today: no notice at all, item 7) |
| 13 | NVIDIA device code shipped without the EULA terms passed on; unused cudart; cuda-keyring without copyright; nv-codec-headers MIT notice missing | -cuda13 | CUDA EULA 1.1.1(c), 1.1.2(b)(e); MIT | CUDA EULA v13.4 | weak |
| 14 | `vmaf-mcp` / `vmaf-tune` metadata says BSD-2-Clause-Patent, files EUPL-1.2, no licence file | PyPI rc1, rc2; -server | consistent licence statement; EUPL Art. 5 notices | ADR-1250 | weak |
| 15 | Training data terms (Netflix Public Dataset, DUTS-TR, KoNViD-1k) vs BSD-2-Clause-Patent weights; KoNViD "CC BY 4.0" claim unsupported | tiny models | dataset terms | dataset pages | weak |
| 16 | OCI `licenses` label `BSD-2-Clause-Patent` on every image; docs footer says BSD-2-Clause-Patent only | images, docs site | accurate statement | ADR-1250 | cosmetic |
| 17 | No SPDX SBOM attested with `actions/attest` on production digests (CycloneDX via cosign exists) | all images, release assets | ADR-1503 R5 (project rule, not a licence) | — | cosmetic |
| 18 | Dev container carries non-distributable CUDA toolkit / Intel basekit files; private today | vmafx-dev-mcp | CUDA EULA 1.1.1, Attachment A; Intel EULA 2.1 | — | weak (breaks if made public) |
| 19 | `libvmaf.so.3.0.0.p/` object files shipped | vmafx-node, go-server | — | — | cosmetic |
| 20 | Untagged image versions from partial runs stay pullable by digest with the same gaps | GHCR | as above | — | weak |

## Licence sources (fetched / read 2026-10-04 unless noted)

- CUDA Toolkit EULA v13.4, last updated 2026-01-26: <https://docs.nvidia.com/cuda/eula/index.html> 1.1.1(c), 1.1.2(a)-(f), 2.2, 2.3, Attachment A (libcudart.so, libcuda.so, libnvidia-ptxjitcompiler.so, libdevice.10.bc); the same text is the copyright file of `cuda-cudart-13-4` 13.4.92-1 in the image.
- ROCm 10.0.0 licensing: <https://rocm.docs.amd.com/en/latest/about/license.html> (ROCgdb GPL-3.0; ROCprof Trace Decoder and AOCC under separate AMD agreements; proprietary components only from repo.radeon.com).
- ROCprof Trace Decoder: <https://github.com/ROCm/rocprof-trace-decoder> `LICENSE` (AMD Software End User License Agreement; §2, §3.2).
- Intel End User License Agreement for Developer Tools (Version August 2024) 2.1.D and Intel Simplified Software License (Version October 2022): texts shipped in the published oneAPI image (`/opt/intel/oneapi/redist/share/doc/*/licensing/`); ADR-1503 read the 2026.0 compiler's copy on 2026-10-03.
- FFmpeg n9.0.2 `configure` (<https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0.2/configure>): `--enable-nonfree ... the resulting libs and binaries will be unredistributable`, `EXTERNAL_LIBRARY_NONFREE_LIST`, `HWACCEL_LIBRARY_NONFREE_LIST`; <https://ffmpeg.org/legal.html> checklist.
- SVT-AV1 v4.2.0 `LICENSE.md` (BSD-3-Clause-Clear, Copyright (c) 2021 Alliance for Open Media) and `PATENTS.md` (AOM Patent License 1.0).
- KoNViD-1k database page <http://database.mmsp-kn.de/konvid-1k-database.html>.
- Upstream model licences: richzhang/PerceptualSimilarity `LICENSE` (BSD-2-Clause, Copyright (c) 2018 Richard Zhang, Phillip Isola, Alexei A. Efros, Eli Shechtman, Oliver Wang); soCzech/TransNetV2 `LICENSE` (MIT, Copyright (c) 2020 Tomáš Souček); m-tassano/fastdvdnet `LICENSE` at c8fdf61 (MIT, Copyright 2024 Matias Tassano).
- Go module licences: each module's `LICENSE*` / `NOTICE*` from the Go module cache at the versions in the binaries' build info; `cloudsoda/sddl` LGPL-3.0; hashicorp and riverqueue modules MPL-2.0.
- GPL-2.0, GPL-3.0, LGPL-2.1, LGPL-3.0, Apache-2.0, MPL-2.0 texts: `/usr/share/common-licenses/` of the images and the module LICENSE files.
- `LICENSES/EUPL-1.2.txt` Art. 5; `LICENSE` (BSD-2-Clause-Patent, Netflix); `LICENSES/LicenseRef-LIVE-BRISQUE.txt` (ADR-1507).

## Already-published artifacts and proposed remedies

These predate ADR-1513; whether and how to remedy them is the maintainer's
decision. Nothing was deleted or republished by this audit.

| Artifact | Breaks | Proposed remedy |
|---|---|---|
| `ghcr.io/vmafx/vmafx:v1.0.0-rc.1-rocm10`, `:v1.0.0-rc.2-rocm10` | redistributes the ROCprof Trace Decoder, whose licence forbids distribution; ROCgdb (GPL-3.0) without source | withdraw both tags and their untagged digests; publish the next ROCm image from the gated recipe |
| `ghcr.io/vmafx/vmafx-node:v1.0.0-rc.1`, `:v1.0.0-rc.2` | FFmpeg built `--enable-nonfree` ("not legally redistributable"); GPL/LGPL libraries and rclone's LGPL-3.0 module without source; no notices | withdraw; republish from the gated recipe (no `--enable-nonfree`, `-source` companion) |
| CPU, server, CUDA, oneAPI images of rc.1 and rc.2; `vmafx-server`, `vmafx-operator` rc.1 and rc.2 | no notices (BSD, ISC, MIT, Apache-2.0 NOTICE), no source for the GPL/LGPL base packages, grafted GCC runtimes and MPL-2.0 modules; Intel runtime without the EULA 2.1.D(2) pass-on | either withdraw and rebuild as `v1.0.0-rc.3` from the gated recipes, or publish `<tag>-source` companions for the existing digests and a notices file on each release page; rc.3 from the gated recipes is the cleaner path |
| GitHub release assets rc.1, rc.2 (`libvmaf.so*`, `vmaf`, `models.tar.gz`) | no notices or licence texts | add `THIRD_PARTY_NOTICES.txt` and the licence texts as assets of both releases |
| PyPI `vmaf-mcp` 1.0.0rc1, 1.0.0rc2 | metadata says BSD-2-Clause-Patent for EUPL-1.2 files, no licence file | PyPI files are immutable: yank both, or leave them and ship corrected metadata from rc3 on; the files keep their EUPL-1.2 headers |
