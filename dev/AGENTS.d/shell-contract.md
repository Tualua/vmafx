---
paths:
  - dev/Containerfile
invariant: Declare SHELL with pipefail across build stages; array-based Go output count; explicit collection failure.
---
<!-- markdownlint-disable MD013 -->
# SHELL declaration and failure handling (DL4006)

## SHELL / hadolint DL4006

- Declare `SHELL ["/bin/bash", "-o", "pipefail", "-c"]` explicitly in
  `build-deps`, `release-build`, `gpu-sdks`, `libvmaf-build`,
  `go-build`, and `dev-mcp`. Each executing stage then exposes pipeline failure
  contract to both builder and static analysis without depending on
  parent-stage tracking.
- Keep Go output-count guard as Bash array, not parsed `ls` output.
  Artifact builder returns to `USER vmaf` after privileged
  compilation; final runtime also remains `USER vmaf`.
- Keep collection failure handling explicit: import failure stops
  layer; pytest collection failure prints captured diagnostics, exits
  nonzero.
