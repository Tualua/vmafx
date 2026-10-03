<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1505: An Intel GPU tester image with a backend-neutral GPU section in the report

- **Status**: Accepted
- **Date**: 2026-10-03
- **Deciders**: Lusoris
- **Tags**: ci, docker, sycl, testing, parity, fork-local

## Context

The SYCL twins are proven on the project's Arc A380, and since 2026-10-03 on an
Arc B580 and an Arc Pro B60 (ADR-1501). The Xe-LP half of
`T-SYCL-ROW-KERNELS-SG16-OTHER-DEVICES-2026-10-02` needs an integrated GPU the
project does not own: an outside tester's Intel Core with its UHD 770, whose
operating system may be Linux or Windows with WSL2. The tester must run one
command and build nothing, and the result must say, row by row, what it
measured. Two more kits (CUDA and HIP, a Windows bundle) follow, so the report
needs one GPU section that any backend fills, not a third Metal-style section.

The tester image of ADR-1492 is CPU-only. The development container carries
every SDK and is about 70 GB; a hosted runner cannot pull it. The oneAPI release
image (ADR-1368) shows how to build a SYCL binary on Debian 13 at the pinned
versions, but its runtime stage installs Intel's runtime meta-package, 1.1 GB
that includes an OpenCL CPU device, an OpenMP offload library and an LTO plugin,
none of which a GPU run loads.

## Decision

We will publish `ghcr.io/vmafx/vmafx:<describe>-tester-sycl` (linux/amd64) from
a `final-sycl` target of `docker/Dockerfile.tester`, built and published by
`docker-publish-tester.yml` in new jobs that follow the CPU image's pattern
(dispatch on master with `ref`, the `tester-publish` environment, keyless
signature, provenance, an attested SPDX SBOM and a `-source` image) under the
licensing rules of [ADR-1503](1503-tester-artifact-licensing.md): the image is the
artifact `sycl-image` of `licensing.json`, and its build cannot finish without the
licence check. The build stage is the oneAPI release builder (Debian 13,
`install-intel-oneapi.sh --mode=builder`, `install-intel-ocloc.sh --components
build`): the SYCL build with the default AOT list plus SPIR-V, the `gpu` and
`sycl` suites' executables and shell tests, and the parity gate. The runtime
stage holds the Intel SYCL runtime files the compiler's `credist.txt` lists and
the build needs (`libsycl`, the Unified Runtime loader and Level Zero adapters,
the compiler runtime libraries), copied unmodified with their licence texts
(`tools/rc1-tester/image/sycl-runtime.json`), UMF and hwloc, and from the pinned
compute-runtime release only the Level Zero GPU driver, IGC core, gmmlib and the
Level Zero loader: the offline compiler, the OpenCL ICD and IGC's OpenCL front
end are purged, because SPIR-V reaches the GPU through Level Zero and IGC alone
(measured on the A380 with a SPIR-V-only probe). The report gains schema version
3 and a `gpu` section (`hw_gpu.py`) that a backend fills through one small
interface (`GpuBackend`: discover devices, pin a run to one, parse audit
output, name a row map): per device the twins against the CPU of the same image
per fixture and per parity-gate feature at the gate's bound for that twin, the
gate's cells of the backend held exact, the device tests, the audits and the
state rows of that device's family. The SYCL backend finds every Level Zero GPU
(at most four), pins each run with `ONEAPI_DEVICE_SELECTOR=level_zero:<n>`,
records whether it reached the GPU through a Linux render node or WSL2's
`/dev/dxg`, and reads the GPU IP version to place the device in a family
(`ocloc ids`): the row map (`sycl-rows.json`) applies the SG16 row's Xe-LP part
only to an Xe-LP or Xe-LPG device. No usable device is `no_device` with the
missing `docker run` option as the reason, not a failure, as for Metal.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Build `FROM` the development container | One toolchain definition | About 70 GB; no hosted runner pulls it; a dev image is not a release recipe | The release builder installs the same pinned versions on Debian 13 |
| Install Intel's runtime meta-package, as the oneAPI release image does | One installer line, already used | 1.1 GB; ships an OpenCL CPU device, OpenMP offload and an LTO plugin; the tester licence rules allow only `credist.txt` files the program needs | Copying the listed files measures 77 MB and ships nothing unused |
| Keep the OpenCL ICD and IGC's OpenCL front end | The compute runtime's full runtime set | 420 MB more; nothing in libvmaf uses OpenCL | Measured unnecessary for SPIR-V and for every device test |
| A Metal-style `sycl_*` set of report sections | No schema refactor | A third backend-specific section per kit; the CUDA and HIP kits would copy it | One `gpu` section with a backend interface |
| Run only the device chosen by default | Shorter run | The tester's host has an Arc B580 next to the UHD 770; the default device would be the dGPU and the Xe-LP row would stay open | Every Intel GPU runs, each pinned |
| Count `no_device` as a failure | A run without `--device` is noticed | The hosted runner has no GPU and must still check the image; Metal set the precedent | The reason names the missing option, and the CI step asserts it |
| Link the device tests against the shared library to save space | About 900 MB less | Changes how every unit test links internal symbols | Out of scope; the image is about 0.95 GB to download |

## Consequences

- **Positive**: one command closes the Xe-LP part of the SG16 row on a tester's
  UHD 770 and measures every SYCL twin on any Intel GPU; the CUDA and HIP kits add
  a backend module and a row map, not a report format. The A380 run of the
  documented Linux command passes in about 90 s: 69 device tests, the scratch
  audit (125 kernels, none in scratch memory), every twin identical to the CPU
  on four fixtures, the gate's cells at 0 (ciede at its `1e-9` bound, 0
  measured).
- **Negative**: the image is about 1.6 GB unpacked and 0.95 GB compressed,
  mostly the device test executables, each carrying the AOT images of libvmaf
  (about 10 MB each). The WSL2 path rests on a probe of the same compute
  runtime under Docker Desktop on Windows 11 (Research-2128), not on a run of
  this image there; the report records the path it took so the first WSL2 report
  shows it.
- **Neutral / follow-ups**: the licence record gains a `dpkg-foreign` component
  kind (vendor packages installed by dpkg, which carry no Debian copyright file
  and no Debian source) and pinned `fetched_texts`; a backend plugs in as
  described in the maintainer notes; the Metal sections keep their schema-2 form
  and may move into `gpu` later.

## References

- `req` (maintainer brief, 2026-10-03, paraphrased): an Intel GPU tester kit an
  outsider runs with one command and no build, first for a UHD 770 on Linux or
  WSL2; it closes the Xe-LP half of the SG16 row, measures every SYCL twin on any
  Intel GPU, and its report section is generic so the CUDA and HIP kits reuse it.
- [ADR-1492](1492-tester-image-arm64-report.md), [ADR-1493](1493-macos-tester-bundle.md),
  [ADR-1496](1496-metal-gate-in-tester-bundle.md), [ADR-1368](1368-oneapi-release-image-debian13.md),
  [ADR-1468](1468-sycl-sub-group-sizes-every-aot-target.md), [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-1501](1501-sycl-float-adm-terms-large-grf-xe2.md), [ADR-1503](1503-tester-artifact-licensing.md),
  [Research-2128](../research/2128-oneapi-release-image-runtime.md) (the compute runtime under WSL2).
- Intel compute-runtime release 26.35.39758.10 notes (supported platforms, WSL
  testing) and `documentation/WSL.md` of `intel/compute-runtime`.
