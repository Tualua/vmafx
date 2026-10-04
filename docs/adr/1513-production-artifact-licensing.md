<!-- markdownlint-disable MD013 MD060 -->
# ADR-1513: The production images, release assets and Python packages follow the tester licensing rules, checked by the same tool

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: license, compliance, supply-chain, ci, docker, release, python, fork-local

## Context

ADR-1503 set the licensing rules for the tester artifacts: only what the report
needs ships, every file has a recorded licence, a gate refuses the build
otherwise, notices and licence texts travel inside the artifact, copyleft object
code has its corresponding source published at the same place, and an SPDX SBOM
is attested. The production artifacts were not covered. An audit of everything
published from `master` and the release tags on 2026-10-04
([Research-2140](../research/2140-production-artifact-licence-audit.md)) found
that none of them met the licences of what they contain:

- No image and no native release asset carries the notices of the VMAFx
  binaries: Netflix's BSD-2-Clause-Patent code and the BSD-3-Clause,
  BSD-2-Clause, ISC and MIT code of IQA, libsvm, libjxl, Xiph, Daala, dav1d,
  x264 and CIEDE2000 each require the notice in the documentation of a binary
  distribution.
- Every image ships GPL and LGPL object code from its Debian or Ubuntu base
  (glibc and the GCC runtime even in the distroless images) with no source.
- The MCP server image ships GPL and LGPL libraries grafted into the numpy and
  scipy wheels without source, its CPython without the notices of the software
  it incorporates, and its build tools in the runtime venv.
- `vmaf-mcp` on PyPI and `vmaf-tune` declare `BSD-2-Clause-Patent` while their
  files are EUPL-1.2 (ADR-1250), and neither ships a licence file.
- The GPU, Go and node images have larger gaps of their own (a binary-only AMD
  library whose licence forbids redistribution, an FFmpeg build that declares
  itself not redistributable, Go modules under Apache-2.0, MPL-2.0 and LGPL-3.0
  without notices or source); they are fixed in their own changes under this
  decision.

The maintainer's decision (2026-10-04) was to audit first and then give the
production artifacts the treatment the tester artifacts got, on the standing
condition that no licence is broken.

## Decision

Every published production artifact follows ADR-1503's rules, enforced by the
same tool and record (`tools/rc1-tester/image/licensing.py`,
`licensing.json`), with one artifact kind per published image or archive:

1. **Images.** Each published target writes
   `/usr/local/share/vmafx/licenses/THIRD_PARTY_NOTICES.txt` and every licence
   text, and its final stage copies the receipt of a licence-check stage, so it
   cannot be built without passing the check. A distroless image has no
   interpreter, so its notices are written on a copy of its tree in a Python
   stage and copied back. `licensing.py` reads a distroless image's package
   records from `var/lib/dpkg/status.d/` (one stanza and one `md5sums` file per
   package) and claims a package's own symlinks through the file or directory
   they point at.
2. **Source.** Each image publishes its corresponding source in the same GHCR
   package as `<tag>-source` (CPU), `<tag>-server-source` (MCP server) and so on,
   from a `*-source-export` stage: the Debian or Ubuntu source packages of every
   installed package at the installed version and every recorded source archive.
3. **SBOM.** An SPDX SBOM (Syft v1.51.1) of every platform image is attested on
   its digest with `actions/attest`; the CycloneDX attestation stays. The
   steps live once, in the composite action
   `.github/actions/image-licence-artifacts`, which also pushes, signs and
   attests the source image.
4. **Only what runs ships.** The MCP server's wheels are built in a build-only
   venv; the runtime venv holds the locked runtime dependencies and the two
   wheels.
5. **Python packages** declare the licences their files carry (the union of the
   files' SPDX identifiers: `EUPL-1.2` for `vmaf-mcp`, `EUPL-1.2 AND
   BSD-2-Clause-Patent` for `vmaf-tune`) and ship each text through PEP 639
   `license-files`, byte for byte the repository's `LICENSES/` copy; a test
   recomputes the union from the sources.
6. **Labels and pages.** The `org.opencontainers.image.licenses` label is the
   licence set of the compiled VMAFx files, not `BSD-2-Clause-Patent`; the
   documentation site's footer and its licensing page state the per-file rule
   of ADR-1250.

A test holds every `(Dockerfile, target)` the publish workflows push to a
licence-check receipt in its final stage; targets whose gate lands with a later
change of this train are listed, and the list only shrinks.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Notices on the GitHub release page only | No image change | The notices must accompany the binary (BSD clause 2: "in the documentation and/or other materials provided with the distribution"); an image pulled from GHCR has no release page; does nothing for source | Not compliant for images |
| A written offer for source instead of `-source` images | No upload | GPL-2.0 3(b) binds the maintainer for three years to an archive that must exist anyway; LGPL-2.1 section 4 has no offer route for the library itself (ADR-1503) | Same reasoning as ADR-1503 |
| Move the CLI image off distroless to `debian:13-slim` so `dpkg` records exist | No new reader | A larger image with a shell and package manager, a change of the security posture ADR-0698 chose | Reading `status.d` is 20 lines |
| Add a busybox layer to the distroless image to run the notices in place | One stage less | Ships a shell and a GPL-2.0 binary into an image that has none | The copy-and-copy-back stage ships nothing extra |
| A separate licence tool for production artifacts | No coupling to the tester tree | Two implementations of one behaviour (HISS-19); the tester tool already handles dpkg, wheels, grafts, vendor runtimes | Extended instead |
| Copy the SBOM and source steps into each publish job | No composite action | About 60 lines repeated per image, eight images (HISS-19) | One composite action |
| Declare `vmaf-mcp` and `vmaf-tune` as `EUPL-1.2` only | Simplest metadata | `vmaftune/executor.py` keeps BSD-2-Clause-Patent (ADR-1250 left it); the metadata would understate it | The union, recomputed by a test |

## Consequences

- **Positive**: each production image and package says what it contains and
  under which terms, carries the texts, and has its source and SBOM next to it;
  a new library, wheel or file without a recorded licence stops the release
  build instead of reaching users.
- **Negative**: a source image per published image (the CPU one is about
  170 MB, the server one about 660 MB with the three GCC source RPMs); a base
  image or lock bump that brings a new licence or grafted library needs a
  `licensing.json` entry before the release builds.
- **Neutral / follow-ups**: the GPU images, the Go service images, the node
  image (FFmpeg) and the native release assets get their gates in the following
  changes of this train; the already-published v1.0.0-rc.1 and rc.2 artifacts
  predate these rules and the maintainer decides how to remedy them
  (Research-2140 lists them with a proposal).

## References

- `Q` (popup 2026-10-04): "Audit now, then fix (Recommended)"; standing
  condition (paraphrased): no licence may be broken.
- [ADR-1503](1503-tester-artifact-licensing.md), [ADR-1250](1250-eupl-fork-relicense.md),
  [ADR-1102](1102-phase4b9-container-only-publishing.md), [ADR-0698](0698-vmafx-production-dockerfile.md),
  [ADR-1347](1347-image-recovery-from-default-branch.md), [ADR-1507](1507-brisque-live-notice-terms.md).
- [Research-2140](../research/2140-production-artifact-licence-audit.md) (the audit, its
  licence sources and fetch dates).
- PEP 639 (`License-Expression`, `License-File`, `licenses/` in the dist-info).
