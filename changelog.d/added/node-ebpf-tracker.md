- **`VMAFX_EBPF_BYPASS=1` starts the node's eBPF descriptor tracker, and a
  host that cannot run it stops the node.** The loader under
  `cmd/vmafx-node/bpf` was never started and `VMAFX_EBPF_MOUNT_PREFIX` was read
  by no code; the tree held a stub instead of the compiled program. The node
  now starts the tracker when asked (storage must mount under the prefix) and
  refuses to start, listing every reason, on a kernel older than 5.15, without
  kernel BTF or visible syscall tracepoints, or without `CAP_BPF` and
  `CAP_PERFMON` (or `CAP_SYS_ADMIN`). The compiled program is embedded. The
  tracker records descriptors only; no read is bypassed. See
  [the eBPF tracker page](docs/development/ebpf-fuse-bypass.md) and
  [ADR-1539](docs/adr/1539-node-ebpf-tracker-wiring.md).
