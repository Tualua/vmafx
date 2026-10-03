<!-- markdownlint-disable MD013 MD060 -->
# ADR-1503: Every published tester artifact carries its licence texts, an attested SPDX SBOM and the source its copyleft parts require, and a gate refuses a file with no recorded licence

- **Status**: Accepted
- **Date**: 2026-10-03
- **Deciders**: maintainer, agent
- **Tags**: license, compliance, supply-chain, ci, docker, release, testing, fork-local

## Context

The project publishes tester artifacts so that people with hardware it lacks
can run the fork: the CPU tester image (ADR-1492) and the native macOS bundle
(ADR-1493) exist, and kits for Intel GPUs (SYCL), NVIDIA GPUs (CUDA), AMD GPUs
(HIP) and native Windows follow. The maintainer's condition for all of them
(2026-10-03) is that no licence is broken.

An audit of the two published artifacts
([research digest](../research/2133-tester-artifact-licence-audit.md))
found that neither met that condition:

- Neither carried the licence texts or copyright notices of the VMAFx binaries
  inside it. `libvmaf` links Netflix's BSD-2-Clause-Patent code and
  BSD-3-Clause, BSD-2-Clause, ISC and MIT code from IQA, Xiph, dav1d, x264
  and the CIEDE2000 maths; each of those licences requires the notice in the
  documentation of a binary distribution. The Netflix test videos
  (BSD-2-Clause-Patent) shipped without their notice too.
- The macOS bundle's interpreter comes from a python-build-standalone
  `install_only` archive, which holds no licence texts, while its
  `libpython` links OpenSSL (Apache-2.0), libffi and expat (MIT), mpdecimal
  (BSD-2-Clause), bzip2 and HACL* (MIT) statically. The same archive was
  published on its own as a release asset.
- The image ships GPL and LGPL object code (Debian's base system, `sqv` with
  its statically linked Rust crates, and LGPL-2.1 `libquadmath` grafted into
  the numpy and scipy wheels) with no source offer. GPL-2.0 section 3 and
  LGPL-2.1 section 4 require the source to accompany the object code or to
  be offered from the same place.
- Neither artifact had an SBOM; the production images do (Syft CycloneDX via
  `cosign attest`), the tester workflows did not.
- `libgomp.so.1` was copied out of a Debian build stage without its
  copyright file.

The GPU and Windows kits bring vendor runtimes under proprietary terms with
their own redistribution lists. A rule written once, from the current vendor
texts, keeps every kit from relearning them.

## Decision

Every published tester artifact follows these rules; the kit agents read them
in this ADR (the internal summary lives outside the tree).

1. **Only what the report needs ships.** No compiler, devel package, header
   set, SDK installer, debug runtime or GPU driver: the driver comes from the
   host (NVIDIA Container Toolkit, `/dev/dri`, `/dev/kfd`). Vendor binaries
   ship unmodified (no `strip`, no `patchelf`): the CUDA EULA (2.3), the Intel
   Simplified Software License and Microsoft's Distributable Code terms each
   require it, and an unmodified LGPL library keeps its corresponding source.
2. **Every file has a recorded licence.** `tools/rc1-tester/image/licensing.json`
   records each component the artifacts contain (name, SPDX licence,
   licence texts, copyright, where its source is) and the paths it owns.
   Debian and Ubuntu packages are recorded by dpkg ownership and their
   `/usr/share/doc/<package>/copyright`, which is never removed; Python
   packages by their `*.dist-info/RECORD` and the licence files in their
   dist-info, which are never removed. A shared library grafted into a wheel
   is recorded by name and, when its licence is copyleft, by its ELF build
   ID together with the source package it was built from. The licences of
   the VMAFx binaries are not listed by hand: they are read from the SPDX
   headers (or `REUSE.toml`) of every repository file the build compiled,
   taken from `ninja -t deps`.
3. **The gate.** `tools/rc1-tester/image/licensing.py check` runs on the
   finished tree of each artifact before anything is published (the image
   build fails in its `licence-check` stage, the bundle script before it
   packs) and exits 1 when a file is claimed by no component, a package has
   no copyright file, a dist-info has no licence file, a grafted copyleft
   library has no recorded source, a compiled file declares a licence the
   component does not, or the notices are missing or stale.
4. **Notices travel inside the artifact.** `licenses/THIRD_PARTY_NOTICES.txt`
   and every licence text (`/opt/vmafx/licenses/` in an image,
   `<root>/licenses/` in an archive), written by `licensing.py notices`. For
   CPython the notices carry the PSF licence and that version's
   `Doc/license.rst` (the incorporated software), pinned by SHA-256, plus the
   HACL\* notice that file omits; for python-build-standalone they carry the
   `python/licenses/` texts of the same release's `full` archive, pinned by
   SHA-256, because the `install_only` archive has none. An interpreter
   archive is never published as an asset of its own.
5. **Source.** The notices name the repository and the exact source commit,
   which is what EUPL-1.2 Article 5 asks of a distributor. Copyleft object
   code (GPL, LGPL, MPL and the like) has its corresponding source published
   at the same place as the artifact for as long as the artifact is
   published: an image gets `<image>:<tag>-source` in the same GHCR package,
   holding the Debian or Ubuntu source packages of every installed package
   at the installed version (`Built-Using` and `Static-Built-Using`
   included) and every recorded non-distribution source archive (URL and
   SHA-256); an archive gets a source tarball on the same release. A
   component whose exact source cannot be identified is not shipped.
6. **SBOM.** Syft (v1.51.1, the version `supply-chain.yml` pins) writes an
   SPDX JSON SBOM of the published artifact. An image's SBOM is attested on
   its digest with `actions/attest` (`sbom-path`, pushed to the registry);
   an archive's is a release asset (`<name>.spdx.json`) and is attested on
   the archive. `actions/attest-sbom` is deprecated and not used.
7. **Vendor runtimes**, from the texts cited below:
   - **NVIDIA.** Only files listed in Attachment A of the CUDA Toolkit EULA
     that the binary links (for example `libcudart`, `libnvrtc` with
     `libnvrtc-builtins`, `libnvJitLink`), unmodified, used only by our
     program, in object code. `libcuda.so` and `libnvidia-ptxjitcompiler.so`
     are on the list but never ship: the toolkit mounts the host driver's
     matching copy. A kit image derived from `nvidia/cuda` uses a
     digest-pinned `-runtime` or `-base` image as its final stage (never
     `-devel`), keeps that base whole and passes on the NVIDIA Deep Learning
     Container License (sections 1.c and 2); the notices carry both NVIDIA
     texts and say that EUPL-1.2 covers only VMAFx files.
   - **Intel.** SYCL runtime files only from the compiler's `credist.txt`
     (`libsycl`, `libur_loader`, `libur_adapter_level_zero`,
     `libsvml`/`libimf`/`libintlc`/`libirng`, `libiomp5` when linked), under
     the Intel End User License Agreement for Developer Tools section 2.1.D:
     executable code, part of our program, passed on under terms that forbid
     reverse engineering and carry Intel's limitation of liability (7.3), no
     Intel marks. The compiler's `LICENSE`, `third-party-programs.txt` and
     `credist.txt` ship in `licenses/intel/`. oneTBB ships in its Apache-2.0
     build. The GPU user-mode stack comes from distribution or Intel packages
     with their copyright files: compute-runtime (MIT), IGC (MIT, with LLVM
     parts under Apache-2.0 WITH LLVM-exception), gmmlib (MIT), the Level
     Zero loader (MIT), Unified Runtime and UMF (Apache-2.0 WITH
     LLVM-exception).
   - **AMD.** `libamdhip64` (CLR, MIT), `libhsa-runtime64` (ROCR, NCSA),
     `libamd_comgr` (its installed `LICENSE.txt`: Apache-2.0 WITH
     LLVM-exception), `librocprofiler-register` and `rocm-core` (MIT), the
     device libraries (NCSA), with the texts from
     `/opt/rocm/share/doc/<component>/`. Their distribution dependencies
     `libnuma1` (LGPL-2.1) and `libelf1` (LGPL-3.0+ or GPL-2.0+) put source
     into the companion image. ROCgdb, the ROCprof Trace Decoder, AOCC and
     the compiler never ship.
   - **Microsoft.** The Windows kit links the C and C++ runtime statically
     (`/MT`) and ships no runtime DLL. If a DLL must ship, it comes from
     `VC\Redist\MSVC\<version>\<arch>\Microsoft.VC14x.CRT` of a licensed
     Visual Studio, unmodified, next to the program in the zip, never from
     `debug_nonredist`, and the notices tell the recipient the terms that
     protect it. The Universal CRT never ships: it is part of Windows 10 and
     later, which ignore an application-local copy.
8. **Fixtures.** The Netflix test videos come from `Netflix/vmaf_resource`
   at a pinned commit (BSD-2-Clause-Patent, Copyright (c) 2020 Netflix,
   Inc.); its licence text ships with them. An artifact holding frames of Big
   Buck Bunny (`testdata/bbb`) carries this notice, which CC BY 3.0 4(a)
   and 4(b) require (licence URI, the licensor's copyright line, and the
   change made):

   ```text
   Big Buck Bunny, (c) copyright 2008, Blender Foundation / www.bigbuckbunny.org,
   licensed under CC BY 3.0 (https://creativecommons.org/licenses/by/3.0/);
   frames cut and converted to raw YUV by the VMAFx project.
   ```

   Any other fixture ships only with a recorded licence.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| A hand-written NOTICE file per artifact, no gate | Small change | Drifts on the next lock or base-image bump; nothing notices a new library | The audit found every gap in a place nobody looked; a list nobody checks repeats that |
| `reuse lint` / REUSE compliance as the gate | An existing tool | REUSE covers the repository, not an image of 30,000 files from Debian, PyPI and vendors | The artifact is the distribution; the gate runs on it |
| Syft's licence fields as the record | One tool | Syft reports what metadata claims and leaves many files unclaimed; it cannot tell a copyleft graft that needs source | Syft writes the SBOM; the record and the gate are ours |
| A written offer (GPL-2.0 3(b)) instead of a source image | No large upload | Binds the maintainer to deliver source on request for three years from an archive that would have to exist anyway; LGPL-2.1 section 4 has no offer route for the library itself | Publishing the source next to the image meets every licence at once |
| Point to snapshot.debian.org and the vendors' source servers | Nothing to publish | GPL-3.0 6(d) still makes us answerable for that server; GPL-2.0 and LGPL-2.1 section 4 accept only the same place | Not compliant on its own |
| Drop the Python golden gate from the image to shed the LGPL wheel libraries | Smaller image, fewer obligations | Removes the numerical ground truth the tester confirms (ADR-1492 kept it for that reason) | Supplying the three source RPMs is cheaper than losing the gate |
| Ship the MSVC runtime DLLs app-local by default | Smaller binaries | Distributable Code terms to pass on, a servicing burden Microsoft advises against | Static `/MT` needs no DLL and no terms |

## Consequences

- **Positive**: each artifact says what it contains and under which terms,
  carries the texts those terms require, and has a verifiable SBOM; a new
  library or wheel without a recorded licence stops the build instead of
  reaching a tester; the kits inherit one checked mechanism.
- **Negative**: a source image per tester image (about 670 MB: 391 MB of
  Debian sources and 282 MB of three source RPMs for the GCC runtimes
  grafted into the numpy, scipy and scikit-learn wheels); a lock or base
  bump that brings a new grafted library or a new licence needs a
  `licensing.json` entry; the macOS build downloads the interpreter's full
  archive for its licence texts.
- **Neutral / follow-ups**: the already-published image and bundle predate
  these rules; whether to withdraw them is the maintainer's decision. Each
  new kit adds its artifact kind, its components and a negative test.

## References

- Maintainer condition, 2026-10-03 (paraphrased): the GPU and Windows tester kits
  may be published only if no licence is broken.
- [ADR-1250](1250-eupl-fork-relicense.md), [ADR-1492](1492-tester-image-arm64-report.md),
  [ADR-1493](1493-macos-tester-bundle.md), [ADR-1496](1496-metal-gate-in-tester-bundle.md).
- Licence texts, all read on 2026-10-03:
  [CUDA Toolkit EULA](https://docs.nvidia.com/cuda/eula/index.html) (v13.4,
  last updated 2026-01-26: 1.1.1, 1.1.2, 2.2, 2.3, Attachment A);
  [NVIDIA Deep Learning Container License](https://developer.download.nvidia.com/licenses/NVIDIA_Deep_Learning_Container_License.pdf)
  (v. 2021-09-14), named by [nvidia/cuda on Docker Hub](https://hub.docker.com/r/nvidia/cuda);
  Intel End User License Agreement for Developer Tools (Version August 2024),
  Intel Simplified Software License (Version October 2022) and `credist.txt`
  of oneAPI DPC++/C++ Compiler 2026.0, read from the installed
  `licensing/2026.0/license.htm` and `compiler/2026.0/share/doc/compiler/`;
  [ROCm licensing](https://rocm.docs.amd.com/en/latest/about/license.html)
  (ROCm 10.0.0) and the installed `/opt/rocm/share/doc/*/LICENSE*` (ROCm 7.2.4);
  [Visual Studio 2022 redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)
  (ms.date 2025-11-11), [Redistribute Visual C++ files](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files)
  (2026-04-13), [Universal CRT deployment](https://learn.microsoft.com/en-us/cpp/windows/universal-crt-deployment);
  [python-build-standalone, Licensing](https://gregoryszorc.com/docs/python-build-standalone/main/running.html);
  [CPython `Doc/license.rst`](https://github.com/python/cpython/blob/v3.14.8/Doc/license.rst);
  [CC BY 3.0 legal code](https://creativecommons.org/licenses/by/3.0/legalcode) (4(a), 4(b));
  [Big Buck Bunny licence](https://peach.blender.org/about/);
  `LICENSES/EUPL-1.2.txt` (Article 5), `LICENSE` (BSD-2-Clause-Patent) and
  `Netflix/vmaf_resource` `LICENSE` at commit `c0ab6adbd7e41bb354f14686ed08500530622bc3`.
- GitHub `actions/attest` v4.2.2 `action.yml` (`sbom-path`); `actions/attest-sbom`
  v4.1.0 prints its own deprecation.
