<!-- markdownlint-disable MD013 MD060 -->
# ADR-1539: vmafx-node starts the eBPF descriptor tracker on request, fails closed when the host cannot run it, and ships the compiled BPF object

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: go, node, ebpf, rclone, security, supply-chain, phase4b, fork-local

## Context

[ADR-0779](0779-ebpf-fuse-bypass.md) and [ADR-0996](0996-ebpf-fuse-bypass-rclone.md)
added a loader (`cmd/vmafx-node/bpf`) for a probe-only eBPF program that
records descriptors opened under the rclone mount. The node never imported
it, `VMAFX_EBPF_MOUNT_PREFIX` was read by no code (docs audit of 2026-10-03,
defect 35), and the tree held a stub instead of the compiled object, so
`Start` always failed. The maintainer decided (popup of 2026-10-04) that the
wiring is implemented, not stubbed, and that a requested path that cannot be
honoured fails.

Wiring it exposed three facts. The hand-written Go mirror of
`struct mount_prefix_t` was 264 bytes against the map's 260-byte value, so
the first map write would have been refused. ADR-0779's read-path bypass (read
the rclone cache file instead of the FUSE path) has no consumer: the vmaf CLI,
a separate process, reads the mounted files, and `pkg/storage` mounts with
`--vfs-cache-mode off`, so no cache file exists. The loader's cache only grew.

## Decision

- `VMAFX_EBPF_BYPASS=1` starts the tracker in fx `OnStart`, between the
  storage layer and the controller client; `OnStop` detaches it after the
  client drained. `VMAFX_EBPF_MOUNT_PREFIX` sets the prefix.
- The node refuses to start when the tracker would observe nothing (storage
  not resolving to mount, or a mount root outside the prefix), and when the
  host cannot run it: `Preflight` lists every failing requirement (kernel
  older than 5.15, no kernel BTF, syscall tracepoints not visible in tracefs,
  no `CAP_BPF`+`CAP_PERFMON` or `CAP_SYS_ADMIN`) before any BPF syscall; a
  relative or over-long prefix is refused instead of truncated. Off Linux the
  request is refused.
- The compiled object is committed: bpf2go (pinned `v0.22.0` in
  `go:generate`) output for the little-endian targets (amd64, arm64 and the
  other little-endian Go architectures; elsewhere the node refuses the
  request), built from the C
  source and a minimal `vmlinux.h` with only the types the program uses, so
  building the node needs no BPF toolchain. The loader uses bpf2go's struct
  mirrors (no hand-written layouts, no `unsafe` decode), and
  `TestEmbeddedObjectMatchesMirrors` checks programs, maps and sizes without
  privileges.
- The cache prunes descriptors the kernel side has closed when it reaches the
  map size (4096).
- Documentation states that the tracker tracks and bypasses nothing; the
  latency benchmark that measured two identical FUSE reads is removed.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep the stub, fail closed with "objects not built" | No binary file in git | The feature can never run in a released node | The object is small (9 KB) and reproducible with the pinned bpf2go and the same clang |
| Generate the object in the image build | No binary in git | Every image build needs clang, libbpf and kernel headers; Go tests could not check the object | Committed object plus a layout test |
| Full `bpftool btf dump` as `vmlinux.h` | Standard practice | 3.5 MB, tied to one kernel, regenerated on every kernel bump | Minimal header; tracepoint contexts are stable ABI |
| Log a warning and run without the tracker when the host cannot | Node still starts | The node would claim a tracker it does not run | Fail closed |
| Implement the read bypass now | Delivers ADR-0779's promise | Needs a cached mount (`--vfs-cache-mode full`) and a scorer that reads cache files instead of paths; a design change of storage and scoring | Out of scope; tracked as an open row |

## Consequences

- **Positive**: a node asked to run the tracker either runs it or stops with
  every reason (verified unprivileged on kernel 7.2.8: tracefs and capability
  refusals). The object agrees with the Go mirrors by test.
- **Negative**: a binary BPF object lives in the tree; regenerating it with
  another clang version changes its bytes. A privileged load has not been run
  on any host.
- **Neutral / follow-ups**: the read-path bypass has no consumer
  (`T-NODE-EBPF-BYPASS-NO-READ-PATH-2026-10-04`); the Helm chart has no value
  for the tracker.

## References

- [ADR-0779](0779-ebpf-fuse-bypass.md), [ADR-0996](0996-ebpf-fuse-bypass-rclone.md),
  [ADR-1526](1526-node-storage-streamed-inputs.md).
- Docs audit defect list of 2026-10-03, defect 35.
- Popup answer of 2026-10-04, "Unwired distributed-platform features": "Implement them now".
