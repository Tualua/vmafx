# eBPF FUSE bypass for vmafx-node rclone mounts

!!! warning "Status: library-only prototype, not wired into vmafx-node"
    The loader package `cmd/vmafx-node/bpf` exists and has its own tests, but
    the `vmafx-node` binary does not import or call it (`cmd/vmafx-node/main.go`
    says it "is not wired into this graph"). Setting `VMAFX_EBPF_BYPASS=1` on
    the node therefore changes nothing; only the package's own tests read it.

The real BPF objects are not committed either: the tree holds a compile-time
stub, so the loader's `Start()` returns an error until `go generate` has
produced them. This page documents the design
([ADR-0779](../adr/0779-ebpf-fuse-bypass.md)) and the parts that exist; the
sections marked "not wired yet" describe the intended behaviour.

vmafx-node fetches video clips through an rclone HTTP-serve FUSE mount. For
clips that rclone has already cached locally, a read through the FUSE daemon
adds a round-trip. A prototype measurement put the p50 latency of FUSE reads at
about 37 times that of direct host-file reads (Research-0733). The eBPF bypass
is designed to remove that round-trip for warm-cache reads.

## How it works

The bypass is a probe-only design. The eBPF program never modifies kernel
memory and never intercepts a data path; it only observes. The four steps:

1. An eBPF tracepoint program (`rclone_bypass.bpf.c`) watches `openat` and
   `close` and records, in a BPF hash map, every file descriptor opened under
   the configured mount prefix (`/rclone-mount/` by default).
2. The Go-side loader (`cmd/vmafx-node/bpf/bypass_loader.go`) reads that map
   and keeps an in-process cache of bypass-eligible descriptors.
3. For a cached descriptor, the application is meant to open the corresponding
   backing cache file directly instead of calling `read(2)` on the FUSE-backed
   descriptor. **Not wired yet:** no code under `cmd/vmafx-node/` performs this
   step today.
4. Anything not in the cache falls through to FUSE as before.

## Requirements (when wired)

The loader's own requirements, enforced by `Start()`:

- Linux kernel 5.15 or newer (BPF CO-RE, ring buffer, `bpf_d_path`).
- `CAP_BPF` (Linux 5.8 and newer) or `CAP_SYS_ADMIN`.
- `/sys/kernel/btf/vmlinux` mounted in the container.

The feature would be off by default, switched on by the environment variable
`VMAFX_EBPF_BYPASS=1` read by `bpf.Enabled()`. No Helm value exists for it; a
Kubernetes deployment would need the capability or `privileged: true` added to
the pod security context by hand.

## Mount prefix

The loader watches `/rclone-mount/` unless told otherwise. The Go constructor
`bpf.New(mountPrefix, log)` takes the prefix as an explicit argument. There is
no environment variable for it: an earlier revision of this page named
`VMAFX_EBPF_MOUNT_PREFIX`, which no code reads.

## Smoke test and benchmark

The package's smoke test compares read latency with and without the bypass. It
needs a live rclone mount and `CAP_BPF`:

```bash
VMAFX_EBPF_BYPASS=1 \
VMAFX_EBPF_SMOKE_RCLONE_MOUNT=/rclone-mount \
go test -v -run TestReadLatencyComparison -timeout 120s \
  ./cmd/vmafx-node/bpf/
```

A sample of the expected output (illustrative numbers):

```text
baseline p50 read latency (no bypass): 370ms
bypass p50 read latency:               10ms
speedup ratio (baseline/bypass):       37.0x
```

## Build and regenerating BPF objects

The BPF object is compiled from `cmd/vmafx-node/bpf/rclone_bypass.bpf.c` with
`clang` and `bpf2go`. A compile-time stub (`rclone_bypass_stub.go`) lets the
package build in CI without the BPF toolchain; it is what the tree contains
today.

To generate the real objects:

```bash
# Install prerequisites (Debian/Ubuntu)
apt-get install -y clang libbpf-dev linux-headers-$(uname -r)

# Regenerate
go generate ./cmd/vmafx-node/bpf/
```

The generated `rcloneBypass_bpf*.go` and `rcloneBypass_bpf*.o` files are not in
the tree yet. Once the bypass is wired, commit them together with the updated
`.c` file.

## Risks and mitigations

| Risk | Mitigation |
| --- | --- |
| Kernel version < 5.15 | `Start()` returns a clear error; bypass is skipped gracefully |
| Missing `CAP_BPF` | Same: error logged, node continues without bypass |
| BPF verifier rejects program after kernel upgrade | `VMAFX_EBPF_BYPASS` off by default; upgrade testing required before enabling |
| Mount prefix misconfiguration | All reads fall through to FUSE as before; no data corruption possible |
| Container security policy blocks BPF | Needs `CAP_BPF` or a privileged container; no Helm value exists yet |

## See also

- [ADR-0779](../adr/0779-ebpf-fuse-bypass.md): design rationale and
  alternatives.
- [ADR-0709](../adr/0709-vmafx-phase4b-distributed-platform.md): Phase 4b
  platform overview.
- [ADR-0713](../adr/0713-vmafx-node-impl.md): vmafx-node worker binary.
- Research-0733: rclone FUSE overhead profiling (37x p50 measurement).
