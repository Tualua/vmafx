- **The Arc runner's systemd unit starts the supervisor of this repository.**
  `dev/systemd/vmafx-sycl-arc-runner.service` named `%h/dev/vmaf/...`, the
  archived repository's path, so the documented install started nothing; it
  names `%h/dev/vmafx/vmafx/...` and the install guide says so. ADR-0931 (MCP
  direct cgo path) is `Accepted` for its implemented Phase 1.
