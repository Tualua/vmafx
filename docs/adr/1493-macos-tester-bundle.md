<!-- markdownlint-disable MD013 MD060 -->
# ADR-1493: macOS tester bundle, an exception to container-only publishing

- **Status**: Accepted
- **Date**: 2026-10-03
- **Deciders**: maintainer, agent
- **Tags**: ci, release, macos, metal, testing, supply-chain, fork-local

## Context

The Metal twins have never run on a device in this project. A tester with an Apple M4
will run a native build if it is packaged for him; he has no Xcode, Homebrew or Python.
ADR-1102 requires published artifacts to come from an image built from the repository's
container, which cannot produce a macOS binary.

## Decision

We publish one macOS arm64 tester bundle as a `.tar.gz` built only by
`.github/workflows/macos-tester-bundle.yml` on the hosted macOS arm64 runner, from the
source of any commit reachable from master (input `ref`, a SHA or `master`; or an
existing published `tag`, exactly one of the two) with the dispatching ref's recipe (the
ADR-1347 pattern). A tester artifact is evidence for the commit it was built from, not a
product release, so it need not come from a release tag: a release tag can be hundreds
of commits behind the code a tester is asked to check. The file is named
`vmafx-tester-macos-arm64-<git describe of the commit>` and the prerelease is
`tester-<YYYYMMDD>-<sha8>`. This is an exception to ADR-1102 with these bounds: hosted
runner only, a commit on master, attested
and cosign-signed, tester bundle only (never a release binary, never attached to a
`v*` release), and the exception ends when a macOS build path exists in the container.

The bundle holds `vmaf` with libvmaf linked in statically and Metal on, no ONNX Runtime;
unit executables; fixtures checked against pinned SHA-256 values; and the report program
run by a bundled python-build-standalone interpreter (pinned URL and SHA-256), because
macOS ships no usable `python3` without the command line tools. The same Python report
code, schema and CI validator serve the container and the bundle (HISS-19). The CI
step `scripts/ci/check-macos-bundle-links.sh` asserts that `otool -L` of `vmaf` and of
every test lists only `/usr/lib` and `/System/Library`.

Trust: every Mach-O we built gets an ad-hoc signature (`codesign -s -`; arm64 requires
one); there is no Apple notarization because no Developer ID exists. A file fetched with
`curl` carries no `com.apple.quarantine` attribute and is not subject to the Gatekeeper
check; a browser download is, and `xattr -d com.apple.quarantine` removes the attribute.
Provenance is a GitHub build attestation (`gh attestation verify`) and a cosign keyless
bundle (`cosign verify-blob`), the same tools `supply-chain.yml` uses. `run.sh` starts
the report under `sandbox-exec` with network access denied when macOS offers it.

Publishing runs behind the `tester-publish` environment (master only, the maintainer as
required reviewer), not `release-publish`, which is tag-only and guards product releases:
tester artifacts are evidence for a master commit, so they follow master's protection.
The `tag` input stays on `tester-publish` as well; nothing about it requires
`release-publish`.

The bundle is a release asset of a prerelease `tester-<date>-<sha>` created with
`GITHUB_TOKEN`: such a release starts no other workflow, and release-please follows
`v*` tags only. A workflow artifact would need a GitHub login and expires.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Report driver in `sh` or Perl | Nothing bundled | A second implementation of the JSON, comparison and hash logic | Rejected (HISS-19) |
| Report driver compiled from C | Smaller | Same duplication, much more code | Rejected |
| Bundled interpreter | One implementation, 66 MB before pruning | A third-party binary, pinned by hash | Chosen |
| `.pkg` installer | Familiar | Needs signing identity, writes outside the directory | Rejected |
| Workflow artifact | No release tag | Login required, expires after days | Fallback only |
| Notarization | No Gatekeeper dialog | No Developer ID | Not possible |

## Consequences

- **Positive**: a tester downloads one archive and runs one command.
- **Negative**: the hosted runner cannot show every twin (no GPU on a VM is likely);
  the first real Metal proof is the tester's run. A third-party interpreter is part of
  the bundle.
- **Neutral**: the bundle is an unsigned-by-Apple artifact; the guide says so.

## References

- `req` (maintainer brief, 2026-10-02, paraphrased): package a native macOS build that
  needs nothing installed, built only by the hosted runner, with a checkable provenance.
- GitHub Docs, "Using artifact attestations": `actions/attest-build-provenance` and
  `gh attestation verify <file> -R <owner>/<repo>`.
- GitHub Docs, "Triggering a workflow": events caused by `GITHUB_TOKEN` start no new
  workflow run, except `workflow_dispatch` and `repository_dispatch`.
- Apple Platform Security, "Gatekeeper and runtime protection"; the quarantine attribute
  is set by browsers and not by `curl`.
- [ADR-1102](1102-phase4b9-container-only-publishing.md),
  [ADR-1347](1347-image-recovery-from-default-branch.md),
  [ADR-1492](1492-tester-image-arm64-report.md).
