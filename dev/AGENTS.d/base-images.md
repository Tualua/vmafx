---
paths:
  - dev/Containerfile
  - build-config.env
  - docs/development/base-images.md
invariant: All base images default from build-config.env; no `COPY --from=<external image>`; declare named stages.
---
<!-- markdownlint-disable MD013 -->
# Base images come from `build-config.env` (ADR-1231)

Do not write base image into Dockerfile in this directory. Every
base = `ARG` whose default mirrors root-level `build-config.env`;
edit that file, run `make base-images-sync`, never `ARG` line by
hand.

`COPY --from=<external image>` counts as base-image pin, rejected
with or without digest by
`scripts/ci/check-base-image-single-source.sh`. Declare named stage
instead — `FROM ${CUDA_RUNTIME} AS cuda-runtime-libs`, then
`COPY --from=cuda-runtime-libs …`. BuildKit prunes unused stages, so extra
stage free. Four pins hidden this way were most out-of-date images
in repository.

See [docs/development/base-images.md](../../docs/development/base-images.md).
