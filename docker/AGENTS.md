<!-- markdownlint-disable MD013 -->
# Container image invariants

## Node model root

`Dockerfile.node` stages contents of `model/` via
`cp -r model/. /dist/model/`; do not copy directory itself. Runtime copy
maps that staging root to `/usr/local/share/vmafx/model` = exact
`VMAFX_MODEL_DIR`. Builder must keep asserting `/dist/model/vmaf_v0.6.1.json`
exists, so nested `model/model/` layout fails at build time, not as worker
unable to resolve its packaged model.

## Base images come from `build-config.env` (ADR-1231)

Do not write base image into Dockerfile in this directory. Every base = `ARG`
whose default mirrors root-level `build-config.env`; edit that file, run
`make base-images-sync`, never edit `ARG` line by hand.

`COPY --from=<digest-pinned image>` = base-image pin, rejected by
`scripts/ci/check-base-image-single-source.sh`. Declare named stage instead —
`FROM ${CUDA_RUNTIME} AS cuda-runtime-libs`, then
`COPY --from=cuda-runtime-libs …`. BuildKit prunes unused stages, so extra
stage = free. Four pins hidden this way were most out-of-date images in repo.

CUDA base images (ADR-1306): `CUDA_BUILDER` and `CUDA_RUNTIME` are digest-pinned
Ubuntu 26.04 (`ubuntu:26.04@sha256:...`) and must equal `DEV_BASE` exactly,
including its digest; never re-introduce `nvidia/cuda` base images. narrow
`dev/ubuntu-26.04-cuda.Dockerfile` compatibility image is part of this owner;
Alpine, Arch, and Fedora compatibility files are not. Toolkit compiler and
runtime packages install via `scripts/ci/install-cuda-toolkit.sh`
(`--mode=builder` or `--mode=runtime`) with exact `package=version` operands and
installed-version checks. `build-config.env` owns release lock and exact
toolkit/nvcc/cudart versions; series-only apt package is not pin.
Renovate discovers `CUDA_VERSION` through official NVIDIA redist HTML index
(`custom.nvidia-cuda-redist`), never through `nvidia/cuda` Docker tags.

oneAPI bases (ADR-1368): `ONEAPI_BUILDER` and `ONEAPI_RUNTIME` equal
`RELEASE_BUILDER_BASE` exactly, digest included (gate). Never bring back
`intel/oneapi-basekit` / `intel/oneapi-runtime`: 2025 images carry
compute-runtime 25.18, which segfaulted every Arc B580 SYCL run; 2026 images
cannot pair compiler + runtime on Debian 13 glibc.

See [docs/development/base-images.md](../docs/development/base-images.md).

## oneAPI production image (ADR-1368)

`builder-oneapi2026` runs `scripts/ci/install-intel-oneapi.sh --mode=builder`,
then `scripts/ci/install-intel-ocloc.sh --components build`. `final-oneapi2026`
runs `--mode=runtime`, then `--components runtime`, then purges curl, gpg and
python3 in same `RUN`. Versions come from `build-config.env` only
(`ONEAPI_APT_VERSION`, `ONEAPI_UMF_APT_VERSION`,
`INTEL_ONEAPI_APT_SIGNER_FINGERPRINT`, `INTEL_NEO_VERSION`, `LEVEL_ZERO_VERSION`).
Load-bearing: NEO `runtime` set (B580 crash without it); `intel-oneapi-umf` in
`ONEAPI_RUNTIME_APT_PACKAGES` (adapters need `libumf.so.1`); adapter `ldd`
check; `ldd` + `--version` of `vmaf`; `USER 65532:65532`. `final-oneapi2025`
stays alias stage; publish tags digest `-oneapi2026` and `-oneapi2025`
(HISS-14). Retire alias only in breaking release with `!` + `Migration:` footer.
`scripts/release/tests/test-docker-image-runtime-contract.sh` pins all of it.

## FFmpeg stable-release mirror

`build-config.env` owns `FFMPEG_TAG`; `docker/Dockerfile.node` carries generated default mirror and must not choose release independently. current baseline is `n9.0.2`, and every update must replay all entries in
`ffmpeg-patches/series.txt` cumulatively before mirror changes. Run
`python3 scripts/ci/ffmpeg_patch_stack.py --check` after refresh; per-patch
`git apply --check` does not model stack's cumulative context.

Every maintained FFmpeg builder configures with `--fatal-warnings` and scans
complete compiler log for `warning:`. same contract is mirrored by root CUDA image, `Dockerfile.ffmpeg`, `dev/Containerfile`, and
`docker/Dockerfile.node`; `scripts/ci/test_e2e_runtime_contract.py` pins all
four. Fix new diagnostics in source without warning suppressions or component
removal. Patch 0019 owns 126-diagnostic GCC 14/16 hardening for n9.0.2
baseline. Use `scripts/ci/checkout-annotated-tag.sh` for FFmpeg checkout;
direct shallow clones emit warning for annotated release tag and violate
same zero-diagnostic image contract.

## Partial libvmaf builder closure

`docker/Dockerfile.node` and root `Dockerfile.go-server` copy only source
needed by their `vmaf-builder` stages. Keep that partial context closed over
all configure inputs: both must copy
`scripts/ci/check-msvc-clz-shim.sh` before `meson setup`. Their build package
sets must include `xxd` (otherwise default built-in models silently turn
off) and `make` (GCC's numeric LTO partitioning invokes it; without it linker warns and falls back to serial LTRANS).

Stage `libvmaf.so*` with `cp -a` so SONAME symlink chain survives. Also
stage Meson's generated `meson-private/libvmaf.pc`; never synthesize it from
`VMAFX_VERSION`. pkg-config version is libvmaf interface version
(`3.0.0`), not release-please product tag (`dev` in local build).
`scripts/ci/test_e2e_runtime_contract.py` pins these invariants for both
Dockerfiles.

## Fedora optional SYCL repository

`dev/fedora-40.Dockerfile` writes seven literal oneAPI repository lines with
`printf '%s\n'` inside `ENABLE_SYCL=true`. Keep both signature checks,
repository-write → install → cleanup short-circuiting, and default-off
branch. Literal `\n` text cannot terminate Dockerfile heredoc: breaks parsing
even with branch disabled. Root Dockerfile's redirected while loop unrelated.
See [Research-2056](../docs/research/2056-fedora-scorecard-heredoc.md).
