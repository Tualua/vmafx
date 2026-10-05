<!-- markdownlint-disable MD013 MD060 -->
# ADR-1622: the node's eBPF object is generated at build time with a pinned clang and no object is committed

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: maintainer, agent
- **Tags**: go, node, ebpf, build, supply-chain, scorecard, phase4b, fork-local

## Context

[ADR-1539](1539-node-ebpf-tracker-wiring.md) committed the compiled eBPF object
(`cmd/vmafx-node/bpf/rclonebypass_bpfel.o`, 9 KB) and the bpf2go binding that
embeds it, so that building the node needed no BPF toolchain. OpenSSF Scorecard's
`Binary-Artifacts` check flags any committed ELF: the object took the check from
10 to 9 and the `Scorecard Master Gate` aggregate to 8.38 against its 8.5 floor
(state row `T-SCORECARD-COMMITTED-BPF-OBJECT-2026-10-05`). One point of
`Binary-Artifacts` is 0.143 of the aggregate. The maintainer decided on
2026-10-05 to generate the object at build time instead.

Generation is reproducible: the object built by clang 23.1.1 was byte-identical
to the committed one, and clang 19.1.7 (Debian 13 and Ubuntu 26.04 packages, on
amd64 and arm64) gives one other digest on every host measured
(`a8079aa4e539dc30e5f275a424f1831d911fb0ef72e579c5c8527b8de2415ca0`).

## Decision

No eBPF object is committed. `scripts/dev/gen-node-bpf.sh` is the one
implementation of the generation step (it runs `go generate
./cmd/vmafx-node/bpf/`); every place the node is built or its tests run runs it
first:

- Go CI (`go-ci.yml`), through the composite action `.github/actions/gen-node-bpf`;
- `docker/Dockerfile.node` (`go-builder` stage, which therefore carries
  `clang-19`, `llvm-19` and `libbpf-dev`) and `dev/Containerfile`'s `go-build`
  stage, which every release, dry-run and e2e image build goes through;
- `make node-bpf`, a prerequisite of `go-build` and `go-test`.

The object and a stamp recording the inputs and the clang version are
git-ignored. The bpf2go binding is source, not a binary, and stays committed,
with one change: bpf2go's `go:embed` of the object is rewritten
(`embed_generated_object.sh`, the third `go:generate` directive) to read it
through `embeddedObject()` in `object_embed.go`, whose `go:embed
rclonebypass_bpfel.o*` also matches a committed notice file. So the package, and
every Go tool that loads it (`go vet`, `go list -export ./...` of the locked
`Go API Compatibility` gate, gosec, an IDE), compiles on a checkout that has
not generated the object. A build without the object makes `Loader.Start` fail
with an error naming `make node-bpf` (`requireObject`), so a node asked for the
tracker stops instead of running without it; the tests that need the object skip
naming the same precondition. With `--require-pin` the script also fails (exit
status 4) when regenerating changes the committed binding.

`build-config.env` records the pins (HISS-11): `BPF_CLANG_VERSION` (the exact
`clang --version`) and `BPF_OBJECT_SHA256` (the digest that clang and the libbpf
headers give). bpf2go is not a separate pin: the script runs the
`github.com/cilium/ebpf` module version in `go.mod`, the one the loader links.
With `--require-pin` (CI, the container build, the release path) the script
fails on another clang and on a different digest; without it a contributor's
other clang builds an object and the script says it is not byte-identical to a
release build. A missing clang, `llvm-strip`, BPF target, libbpf header or `go`
fails with a message naming the tool, the pinned version and the install
command; there is no pre-built download and no fallback.

`TestEmbeddedObjectMatchesPinnedDigest` keeps the reproducibility check in `go
test`: when the stamp says the pinned clang built the object, its sha256 must
equal `BPF_OBJECT_SHA256`. With another clang it skips and names both versions.
`scripts/dev/tests/test_gen_node_bpf.py` covers the script's refusals with a
fake clang, a fake `go` and a planted digest mismatch.

This supersedes the "commit the compiled object" part of ADR-1539 (the object,
its regeneration rule and its negative consequence; the committed binding stays) and the "production images
must include the real object" shape of ADR-0779's build notes. The rest of both
ADRs stands.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep the committed object, accept `Binary-Artifacts` 9 | No toolchain change | The aggregate sits near its floor with no margin | Maintainer chose to generate (popup of 2026-10-05) |
| Generate at build time, pinned clang and digest (chosen) | No binary in git; same bytes on every host with the pinned clang; one script | Every Go build needs clang, llvm and libbpf headers; the package does not compile without generation | Chosen |
| Generate at build time, no digest | Simpler | A silently different clang ships a different object | Reproducibility check kept |
| Download a pre-built object at build time | No local toolchain | Unpinned remote binary, no provenance | Violates HISS-11; excluded by the task |
| Generate both the object and the binding, git-ignore both | Nothing generated is committed | The package does not compile until generated, which fails the locked `Go API Compatibility` gate (`go list -export ./...`, `praetor-api.yml` cannot be edited here) and every un-generated `go vet` | Binding committed, embed rewritten |
| Commit the binding unchanged (it embeds the object) | No rewrite step | The embed of a missing file is a compile error, same failure as above | Rejected |
| Committed stub loader behind a build tag, generated files behind the opposite tag | Compiles without clang | Every build path needs the tag; a forgotten tag ships a node with no tracker; a second copy of the struct mirrors, the drift ADR-1539 fixed (264 against 260 bytes) | Rejected |

## Consequences

- **Positive**: no committed ELF, so Scorecard's `Binary-Artifacts` reads 10 again;
  the object is built from the source in the same build that embeds it; the
  digest check catches a source, header or flag change that was not intended.
- **Negative**: a node built without running the generator has no tracker and
  refuses `VMAFX_EBPF_BYPASS` naming the generator (every supported build path
  runs it); producing the object needs clang 19 (or another clang, with a
  notice), `llvm-strip` and libbpf headers. A change to the C source,
  `vmlinux.h` or the flags changes the digest and may change the binding:
  re-record `BPF_OBJECT_SHA256` and commit the regenerated binding in the same
  PR. The committed binding is one file bpf2go wrote and one script rewrote.
- **Neutral / follow-ups**: the pin follows Debian 13's `clang-19` and Ubuntu
  26.04's; a clang bump re-records both pins. Renovate does not see them.

## Supply-chain impact

- **New dependencies**: build time only, `clang-19`, `llvm-19`, `libbpf-dev` from
  the distribution archive of the build image (Debian 13 or Ubuntu 26.04); none
  reaches a runtime image.
- **Removed dependencies**: none. The committed 9 KB object leaves the tree.
- **Build-time fetches**: none new; bpf2go comes from the module cache the Go
  build already uses (go.sum verified).

## References

- Maintainer popup answer of 2026-10-05, Scorecard `Binary-Artifacts` and the
  committed eBPF object: "Generate it at build time (Recommended)".
- [ADR-1539](1539-node-ebpf-tracker-wiring.md), [ADR-0779](0779-ebpf-fuse-bypass.md),
  [ADR-1559](1559-ebpf-kernel-licence-string.md).
- `docs/state.md` row `T-SCORECARD-COMMITTED-BPF-OBJECT-2026-10-05`.
