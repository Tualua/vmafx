- **The Go package documentation says how `vmafx-mcp` and `vmafx-server`
  reach libvmaf.** `pkg/libvmaf` and the MCP server's tool comment claimed the
  binaries do not link `libvmaf.so` at run time; they do, through cgo
  (`ScoreDirect`, `StreamScorer`, `DNNSession`), so the library must be
  installed next to them. The package documentation now lists all four paths
  into libvmaf, and a contract test fails when a Go package that links the
  library claims otherwise.
