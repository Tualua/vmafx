- `list_backends`, `probe_backend` and `vmaf_version` of both MCP servers (Python
  and Go) now take a GPU backend as available only when `vmaf --list-backends`
  reports it usable. They read `vmaf --help` before, which names every backend
  on every build, so a CPU-only `vmaf` was reported with CUDA, SYCL, HIP and
  Metal and the backend allowlist admitted them. A `vmaf` that cannot print the
  report is treated as CPU-only.
