- **A dev container that builds its own toolchain image under rootless Podman
  (ADR-3520).** `.devcontainer/podman/devcontainer.json` builds the `gpu-sdks`
  stage of `dev/Containerfile` on your machine (CUDA, oneAPI, Intel NEO and
  `ocloc`, ROCm), passes `/dev/dri` through and needs no access to the private
  `vmafx-dev-mcp` image. `dev/Containerfile` now also builds with Podman: the
  Intel NEO step reads its optional GitHub token from the secret's file mount
  ([Podman dev container](docs/development/ide-setup.md#podman-dev-container)).
