<!-- markdownlint-disable MD013 MD060 -->
# Research-2133: Licence audit of the published tester image and macOS bundle

Audit date 2026-10-03. Input to [ADR-1503](../adr/1503-tester-artifact-licensing.md).
Every row comes from the published bytes, not from the recipes: the image was
pulled per platform and flattened (`skopeo copy`, `docker export`), the release
assets were downloaded with `gh release download` and unpacked. Licence terms
were read from the texts cited in ADR-1503's references.

## What was inspected

| Artifact | Identity | Files |
| :--- | :--- | ---: |
| Tester image `ghcr.io/vmafx/vmafx:v1.0.0-rc.2-293-gc12763f3f-tester` | index `sha256:cf1509de…`, amd64 `sha256:b5afa40c…`, arm64 `sha256:6e6b75f5…`; base `python:3.14-slim` (Debian 13) | 14,327 (amd64), 14,312 (arm64) |
| Release `tester-20261003-c12763f3` | `vmafx-tester-macos-arm64-v1.0.0-rc.2-293-gc12763f3f.tar.gz` (checksum OK), `pbs.tar.gz`, two cosign bundles, `bundle-files.txt`, `report.json` | 667 in the bundle |

## Tester image

| Component | Version | Licence | Obligation for a binary distribution | Met? |
| :--- | :--- | :--- | :--- | :--- |
| `vmaf`, `libvmaf.so.3`, 37 unit tests (`/opt/vmafx/build`, `/opt/vmafx/tests`) | source `c12763f3f` | EUPL-1.2 AND BSD-2-Clause-Patent AND BSD-3-Clause AND BSD-2-Clause AND MIT AND Unlicense, plus ISC on amd64 (from the SPDX headers of the 369 repository files the build compiles, `ninja -t deps`) | BSD, ISC, MIT: copyright and licence text in the documentation; EUPL-1.2 Art. 5: licence copy and source location | **No**: no licence text or notice in the image; only the OCI label `BSD-2-Clause-Patent AND EUPL-1.2` |
| Models (`/opt/vmafx/model`) | tree | BSD-2-Clause-Patent (Netflix) | notice | **No** |
| Report and harness source (`/opt/vmafx/tester`, `python`, `compat`) | tree | EUPL-1.2, BSD-2-Clause-Patent | keep notices | Partly: per-file SPDX headers kept; header-less files rely on `REUSE.toml`, which is not shipped |
| Netflix test videos (36 files) | `Netflix/vmaf_resource` `c0ab6adb` | BSD-2-Clause-Patent, Copyright (c) 2020 Netflix, Inc. | notice | **No** |
| CPython (`/usr/local`) | 3.14.8 | PSF-2.0 and the incorporated software of `Doc/license.rst` (expat, libmpdec, mimalloc, HACL\* …) | keep the PSF notice; notices of incorporated software | Partly: `LICENSE.txt` (PSF part, 277 lines) shipped; incorporated-software notices not |
| Debian packages | 87 binary, 61 source packages (`grep` differs by a binNMU between architectures) | GPL-2.0, GPL-3.0, LGPL-2.1, LGPL-3.0, BSD, MIT, … | `/usr/share/doc/<pkg>/copyright`; GPL-2.0 s.3 / GPL-3.0 s.6 / LGPL-2.1 s.4: source with the object code, from the same place | Copyright files: **yes** (87 of 87). Source: **no** |
| `sqv` and its statically linked Rust crates (`Static-Built-Using`, `Built-Using: rustc`) | 1.3.0-3 | LGPL-2.0-or-later (Sequoia) and MIT / Apache-2.0 crates | source of every linked crate | **No** |
| `/opt/vmafx/lib/libgomp.so.1` (copied out of the build stage) | gcc-14 14.2.0-19 | GPL-3.0-or-later WITH GCC-exception-3.1 | copyright file | **No** (no copyright file travels with the copy) |
| Python packages (37 dist-info in `/opt/venv`) | `python/requirements-test-lock.txt` | BSD-3-Clause, BSD-2-Clause, MIT, Apache-2.0 (sureal, with `NOTICE.md`), PSF-2.0 (matplotlib, defusedxml), MIT-CMU (Pillow), Artistic or GPL (text-unidecode) | licence texts; Apache-2.0 s.4(d) NOTICE | **Yes**: every dist-info keeps its licence files |
| Libraries grafted into wheels (OpenBLAS; Pillow's libjpeg, libpng, libtiff, libwebp, libavif, FreeType, HarfBuzz, lcms2, OpenJPEG, Brotli, zstd, liblzma, libxcb, libXau) | per wheel | BSD-3-Clause, MIT, FTL, IJG, Zlib, libpng | notices | **Yes**: in the numpy, scipy and Pillow licence files |
| `libquadmath` grafted into numpy and scipy (amd64) | build ID `c1c5f8d6…` = AlmaLinux 8 `libquadmath-8.5.0-28.el8_10.alma.1`; build ID `549b4c82…` = CentOS 7 `libquadmath-4.8.5-44.el7` | LGPL-2.1-or-later | LGPL-2.1 s.4: complete corresponding source with the object code | **No**: the wheels only point to GCC's git; the Dockerfile also strips the copies |
| `libgfortran`, `libgomp` grafted into numpy, scipy, scikit-learn | AlmaLinux 8 gcc 8.5.0-28 and CentOS 7 `libgfortran5-8.3.1-2.1.1.el7` (build IDs matched on both architectures) | GPL-3.0-or-later WITH GCC-exception-3.1 | notice; the exception covers the combination | **Yes** (notices in the wheels) |
| SBOM | — | — | — | **None** |

## macOS bundle release

| Component | Version | Licence | Obligation | Met? |
| :--- | :--- | :--- | :--- | :--- |
| `build/tools/vmaf` (libvmaf static) and 43 tests | `c12763f3f` | as the image, without ISC (the x86 assembly is not built on arm64) | notices, EUPL licence and source location | **No** |
| Report code (`tester/`), `run.sh`, `README.txt` | tree | EUPL-1.2 | — | headers kept |
| Netflix test videos (7 files) | `c0ab6adb` | BSD-2-Clause-Patent | notice | **No** |
| CPython (`runtime/`) | 3.13.16, python-build-standalone 20261001 `install_only_stripped` | PSF-2.0 | PSF notice; incorporated software | Partly: PSF `LICENSE.txt` only |
| Linked statically into `libpython3.13.dylib` (symbols and version strings checked) | OpenSSL 3.5.9, SQLite, libffi, expat 2.8.5, mpdecimal, bzip2, liblzma, HACL\*, mimalloc | Apache-2.0, public domain, MIT, MIT, BSD-2-Clause, bzip2-1.0.6, 0BSD, MIT, MIT | Apache-2.0 s.4(a) licence copy; MIT / BSD notices | **No**: the `install_only` archive holds no licence texts (the `full` archive of the same release has `python/licenses/` and `PYTHON.json`) |
| macOS system libraries (`libz`, `libedit`, `libncurses`, frameworks) | host | — | not shipped | n/a |
| `pip-26.2.1.dist-info` left after pip was deleted | 26.2.1 | MIT | — | metadata only, licence present |
| `pbs.tar.gz` (separate, cosign-signed asset) | the unmodified `install_only_stripped` archive | as `runtime/` | as `runtime/` | **No**, and the asset is an input, not a product; the build script on master no longer publishes it |
| SBOM | — | — | — | **None** |

No copyleft object code is in the bundle: `_dbm` binds macOS's `dbm_*` in
libSystem (the "GNU gdbm" string is CPython's compatibility constant), and
python-build-standalone disables `_gdbm` and links libedit instead of
readline.

## Gaps

1. No licence texts or notices for VMAFx's own binaries, models and the Netflix
   videos in either artifact.
2. The bundle's interpreter lacks the texts of the libraries linked into it;
   `pbs.tar.gz` repeats that gap as its own asset.
3. CPython's incorporated-software notices (`Doc/license.rst`, and HACL\*,
   which that file omits) are missing in both.
4. The image ships GPL, LGPL and statically linked LGPL Rust code with no
   source, and LGPL `libquadmath` without source (stripped as well).
5. `libgomp.so.1` arrives without its copyright file.
6. Neither publishing workflow writes or attests an SBOM.
7. Nothing stops a new library or package without a recorded licence from
   reaching the next artifact.

## Sizes measured for the fix

`apt-get source --download-only` of the 126 source package versions the image
needs (installed packages plus `Built-Using` and `Static-Built-Using`): all
present in the Debian archive on 2026-10-03, 391 MB. The three source RPMs of
the grafted GCC runtimes: `gcc-8.5.0-28.el8_10.alma.1.src.rpm` (66 MB),
`gcc-4.8.5-44.el7.src.rpm` (79 MB), `gcc-libraries-8.3.1-2.1.1.el7.src.rpm`
(137 MB).
