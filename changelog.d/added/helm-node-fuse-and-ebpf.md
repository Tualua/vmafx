- **Helm: `node.fuse` and `node.ebpf`
  ([ADR-1593](docs/adr/1593-helm-node-fuse-and-ebpf.md)).** `node.fuse` gives
  the node pods `/dev/fuse` through a FUSE device plugin's resource and the
  capability bounding set mount mode needs; `node.ebpf` turns on the eBPF
  descriptor tracker (`VMAFX_EBPF_BYPASS`) with UID 0, `BPF`, `PERFMON` and
  `SYS_ADMIN` and the host's tracefs read-only. `storage.mode: mount` without
  `node.fuse` is now refused at render time; it used to deploy a node that
  could not start.
