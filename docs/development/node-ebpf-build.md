<!-- markdownlint-disable MD013 -->
# Building vmafx-node: the eBPF object

`cmd/vmafx-node` embeds a small eBPF object (the descriptor tracker behind
`VMAFX_EBPF_BYPASS`, see [eBPF FUSE bypass](ebpf-fuse-bypass.md)). The object is
generated from `cmd/vmafx-node/bpf/rclone_bypass.bpf.c` when you build and is
not committed
([ADR-1622](../adr/1622-bpf-object-generated-at-build-time.md)). The Go code
compiles without it, so `go vet` and an editor work on a fresh clone; a node
built without it refuses to start the tracker (`VMAFX_EBPF_BYPASS=1`) with
`this build embeds no eBPF object ... run make node-bpf`, and the tests that
need the object skip naming that command.

## Quick path

```bash
make node-bpf        # needs clang, llvm-strip, libbpf headers, go
make go-build        # runs node-bpf first; so does go-test
```

The container build needs nothing on the host:

```bash
docker build -f docker/Dockerfile.node --target node-cpu -t vmafx-node:dev .
```

## Tools

| Tool | Pinned release | Debian 13 / Ubuntu 26.04 | Arch Linux |
| --- | --- | --- | --- |
| clang with the BPF target | `BPF_CLANG_VERSION` in `build-config.env` (19.1.7) | `apt-get install clang-19` | `pacman -S clang` |
| `llvm-strip` | same LLVM | `apt-get install llvm-19` | `pacman -S llvm` |
| libbpf headers (`/usr/include/bpf/bpf_helpers.h`) | the distribution's | `apt-get install libbpf-dev` | `pacman -S libbpf` |
| bpf2go | `github.com/cilium/ebpf` in `go.mod` | none (run through `go run`) | none |

The script looks for `clang-19`, then `clang`, so a host that carries the pinned
release beside another default `clang` (the GitHub-hosted Ubuntu runner has clang 21
as `clang`) still builds with the pin. `BPF_CLANG` and
`BPF_LLVM_STRIP` name other binaries.

If a tool is missing the script stops with exit status 2 and says which:

```text
gen-node-bpf: clang not found. The node's eBPF object is generated at build time
  and no pre-built object is committed or downloaded; install clang 19.1.7 with the BPF target and libbpf headers
  Debian 13:  apt-get install clang-19 llvm-19 libbpf-dev
  ...
```

## Pinned compiler and digest

The release build, CI and the container build run
`scripts/dev/gen-node-bpf.sh --require-pin`. It refuses a clang other than
`BPF_CLANG_VERSION` and an object whose sha256 is not `BPF_OBJECT_SHA256`
(exit status 3). Debian 13 and Ubuntu 26.04 build the same bytes with clang
19.1.7, on amd64 and arm64. On your own machine another clang (for example 23)
builds a working object; the script prints that it is not byte-identical to a
release build, and `TestEmbeddedObjectMatchesPinnedDigest` skips naming both
versions.

Changing `rclone_bypass.bpf.c`, `vmlinux.h` or the `go:generate` flags in
`cmd/vmafx-node/bpf/gen.go` changes the digest. Generate with the pinned clang
(the container build prints the new sha256 when it fails with exit status 3, or
run the script in a `golang:1.27-trixie` container with `clang-19`, `llvm-19` and
`libbpf-dev` installed) and record it as `BPF_OBJECT_SHA256` in the same pull
request.

## Files

`rclonebypass_bpfel.o` and `.rclonebypass.stamp` (the inputs' hash and the clang
version) are git-ignored. `rclonebypass_bpfel.go`, the bpf2go binding, is
committed source: `embed_generated_object.sh` rewrites its `go:embed` so it reads
the object through `object_embed.go`. The script regenerates when an input
changed, and does nothing otherwise; `--force` regenerates. When the C source
changes the binding's types, the script tells you to commit the regenerated
file, and with `--require-pin` (CI) it fails if you did not (exit status 4).
`go generate ./cmd/vmafx-node/bpf/` is the command underneath and works when
`BPF2GO_CC` and `BPF2GO_STRIP` name the tools.
