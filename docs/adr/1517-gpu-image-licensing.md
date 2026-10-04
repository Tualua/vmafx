<!-- markdownlint-disable MD013 MD060 -->
# ADR-1517: The production GPU images are built on Debian 13, ship only the vendor files libvmaf loads, and share the tester images' licence records

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: license, compliance, supply-chain, docker, cuda, hip, sycl, fork-local

## Context

[ADR-1513](1513-production-artifact-licensing.md) applies the tester licensing
rules of [ADR-1503](1503-tester-artifact-licensing.md) to the production
artifacts. The audit behind it
([Research-2140](../research/2140-production-artifact-licence-audit.md)) found
the three GPU images of `docker/Dockerfile.production-gpu` (`-cuda13`,
`-rocm10`, `-oneapi2026` / `-oneapi2025`) the furthest from that bar:

- `final-rocm10` was `FROM` the whole `rocm/dev-ubuntu-26.04:10.0.0-full` image
  (20.9 GB, 518 packages). It carries `librocprof-trace-decoder.so`, a binary
  AMD library whose licence forbids distributing it, ROCgdb (GPL-3.0) without
  source, the compilers and the SDK libraries for 28 GPU targets, and `amdrocm-*`
  packages without copyright files.
- `final-oneapi2026` installed Intel's runtime meta-package from Intel's apt
  repository: about 1.1 GB with an OpenCL CPU device, OpenMP offload and an LTO
  plugin `libvmaf` never loads, packages without copyright files, and none of the
  pass-on terms that section 2.1.D(2) of the Intel EULA for Developer Tools
  requires for its Redistributables.
- `final-cuda13` installed `cuda-cudart` on Ubuntu 26.04 although `libvmaf`
  links no NVIDIA library; the NVIDIA code the kernels contain (CUDA headers and
  `libdevice`, CUDA EULA 1.1.1(c)) was shipped without the EULA's terms, the
  `nv-codec-headers` MIT notice was missing, and the Ubuntu packages' source was
  not published.

No image had notices, a licence check or a corresponding-source companion.

The GPU tester images ([ADR-1505](1505-intel-gpu-tester-image.md),
[ADR-1509](1509-nvidia-gpu-tester-image.md),
[ADR-1511](1511-amd-gpu-tester-image.md)) had already solved the same problem
for the same binaries, measured on Arc, GeForce and Radeon hardware: Debian 13
builders, runtimes that carry only the vendor files `libvmaf` loads, and licence
records for each of those files.

## Decision

1. **Debian 13 throughout.** Every GPU builder and runtime is the release track's
   `debian:13-slim`. The CUDA builder installs `nvcc` from NVIDIA's `debian13`
   repository (`scripts/ci/install-cuda-toolkit.sh --mode=builder`); the ROCm
   builder streams `/opt/rocm` out of the pinned ROCm image
   (`scripts/ci/install-rocm-from-image.sh --keep-docs`); the oneAPI builder is
   unchanged. The Debian packages' source is then the source the existing
   `licensing.py sources` / `fetch-sources` already publish.
2. **The runtime carries only what `libvmaf` loads.**
   - CUDA: no NVIDIA file. The kernels are fatbins in `libvmaf`, which loads the
     host driver's `libcuda.so.1` at run time; the build fails if a binary links
     an NVIDIA library or an NVIDIA file is in the image. The CUDA EULA (the
     copyright file of the package that installs `libdevice.10.bc`) and the
     `nv-codec-headers` notices ship as texts.
   - ROCm: the files of `tools/rc1-tester/image/hip-runtime.json` in
     `/usr/local/lib/rocm`, copied unmodified with ROCm's licence texts.
   - oneAPI: the files of `tools/rc1-tester/image/sycl-runtime.json` (each
     compiler file listed in the compiler's `credist.txt`) in
     `/usr/local/lib/intel`, plus the compute-runtime GPU stack at the pinned
     `INTEL_NEO_VERSION` with its offline compiler and OpenCL packages purged, as
     in the Intel GPU tester image.
3. **One licence record per component.** The new artifact kinds
   `production-cuda-image`, `production-rocm-image` and
   `production-oneapi-image` take the vendor components of the tester records by
   reference (`{"from": "hip-image", "id": "rocm-sysdeps"}`) and the VMAFx
   components of `production-cli-image` the same way; `licensing.py` expands a
   reference with the record's `rewrite` path prefixes
   (`opt/vmafx/lib/` to `usr/local/lib/`, `opt/vmafx/licenses/` to
   `usr/local/share/vmafx/licenses/`). A vendor runtime's licence, texts, notes
   and the corresponding source of its LGPL libraries are written once.
4. **The ADR-1513 gate.** Each image writes its notices on a copy of its tree,
   `final-cuda13`, `final-rocm10` and `final-oneapi2026` copy the receipt of
   their licence check, and the release workflow attests an SPDX SBOM on each
   digest and publishes `<tag>-cuda13-source`, `<tag>-rocm10-source` and
   `<tag>-oneapi2026-source` through `.github/actions/image-licence-artifacts`.
5. **The ROCm image covers ROCm's own targets.** `hip_gfx_targets` is the
   `dist_amdgpu_targets` list of ROCm's `share/therock/dist_info.json` (25
   targets for 10.0.0), as in the AMD GPU tester image, instead of Meson's
   four-target fallback.
6. **`final-cpu` is removed** from `docker/Dockerfile.production-gpu`: it was an
   unpublished second recipe of the CPU image, which `docker/Dockerfile.production`
   builds.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep the vendor bases and delete the files that may not be distributed | Smallest diff | A file deleted in a later layer stays in the base layer, so the image still distributes the ROCprof Trace Decoder; Ubuntu sources would need a second fetcher (Launchpad) | Does not meet the licence |
| Keep Intel's runtime packages and add the 2.1.D(2) terms | Packages stay dpkg-managed | 1.1 GB of runtime `libvmaf` does not load, every package without a copyright file, and a second record of Intel's runtime next to the tester image's | The credist-listed copy is what the tester image runs on Arc hardware |
| Keep `cuda-cudart` and pass the CUDA EULA terms on | Applications in the image could link cudart | Nothing in the image does; EULA 1.1.2(b) wants the distributed parts accessed only by our application | An unused vendor library is a liability, not a feature |
| Distroless runtimes for CUDA and ROCm | Smaller, as the CPU image | The Intel GPU stack is installed by dpkg, so the three GPU images would differ in base; the tester images verified `debian:13-slim` on devices | One measured base for all three |
| Copy the tester components into the production records | No new code | Two definitions of every vendor component (HISS-19); a ROCm bump would have to edit both | References with a path rewrite keep one |
| Keep Meson's ROCm target fallback | Shorter build | Four targets (gfx90a, gfx1030, gfx1036, gfx1100); RDNA 3.5, RDNA 4 and CDNA 3 cards have no code object | The tester image already builds ROCm's list |

## Consequences

- **Positive**: every GPU image ships only redistributable files with their
  notices, cannot be built when a file has no recorded licence, and publishes the
  source of its copyleft parts. The ROCm image shrinks from the 29 GB dev image to
  the HIP runtime it needs and runs on every GPU target ROCm 10.0.0 supports.
- **Negative**: the images no longer contain the vendor toolchains (`hipcc`,
  `nvcc`, Intel's runtime tree under `/opt/intel/oneapi`); a workflow that used
  them inside the image must use the vendor's image. The oneAPI image offers
  Level Zero only (no OpenCL backend), as the tester image does. Library paths
  move to `/usr/local/lib/rocm` and `/usr/local/lib/intel` (`LD_LIBRARY_PATH`
  is set in the image). The ROCm build compiles 25 targets.
- **Neutral / follow-ups**: the already-published rc.1 / rc.2 GPU images stay as
  they are (`T-PROD-LICENCE-PUBLISHED-RC-ARTIFACTS-2026-10-04`); the
  unpublished node GPU variants in `docker/Dockerfile.node` gain the gate when
  they are published.

## References

- `Q` (popup 2026-10-04): "Audit now, then fix (Recommended)"; standing
  condition (paraphrased): no licence may be broken.
- [ADR-1513](1513-production-artifact-licensing.md), [ADR-1503](1503-tester-artifact-licensing.md),
  [ADR-1505](1505-intel-gpu-tester-image.md), [ADR-1509](1509-nvidia-gpu-tester-image.md),
  [ADR-1511](1511-amd-gpu-tester-image.md), [ADR-1368](1368-oneapi-release-image-debian13.md),
  [ADR-1306](1306-drop-nvidia-cuda-base.md), [ADR-1225](1225-rocm-10-therock-migration.md).
- [Research-2140](../research/2140-production-artifact-licence-audit.md) (the licence
  sources and their fetch dates).
- CUDA Toolkit EULA v13.4 (last updated 2026-01-26) 1.1.1(c), 1.1.2, Attachment A;
  Intel End User License Agreement for Developer Tools (Version August 2024)
  2.1.D; ROCprof Trace Decoder `LICENSE` (AMD Software End User License
  Agreement) 3.2; GPL-3.0 6; LGPL-2.1 6.
