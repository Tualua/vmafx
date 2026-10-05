- **Building `vmafx-node` now generates its eBPF object; none is committed
  ([ADR-1622](docs/adr/1622-bpf-object-generated-at-build-time.md)).** OpenSSF
  Scorecard's `Binary-Artifacts` check flags a committed ELF, so
  `cmd/vmafx-node/bpf/rclonebypass_bpfel.o` is gone from the tree and built
  from `rclone_bypass.bpf.c` by `make node-bpf`
  (`scripts/dev/gen-node-bpf.sh`). Producing it needs clang with the BPF target,
  `llvm-strip` and the libbpf headers; without them the script stops and names
  the tool. The Go code still compiles without the object, and a node built
  without it refuses `VMAFX_EBPF_BYPASS=1`. CI and the container images use the pinned clang 19.1.7
  (`BPF_CLANG_VERSION` in `build-config.env`) and check the object's sha256.
  The node image and its behaviour are unchanged. See the
  [node eBPF build guide](docs/development/node-ebpf-build.md).
