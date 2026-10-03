<!-- markdownlint-disable MD013 MD024 -->
# Tester image and macOS bundle: maintainer notes

What the tester packages are, how they are built and published, what to do by hand,
and how reports reach the tree. The tester-facing steps are in
[the tester guide](../usage/tester-image.md); the decisions are
[ADR-1492](../adr/1492-tester-image-arm64-report.md) and
[ADR-1493](../adr/1493-macos-tester-bundle.md).

## Pieces

| Piece | Where |
| :--- | :--- |
| Container image | [`docker/Dockerfile.tester`](https://github.com/VMAFx/vmafx/blob/master/docker/Dockerfile.tester), build inputs in `tools/rc1-tester/image/` |
| macOS bundle | `scripts/ci/build-macos-tester-bundle.sh`, `tools/rc1-tester/image/macos/` |
| Report program | `tools/rc1-tester/src/vmaf_rc1_tester/hw_*.py`, launcher `tools/rc1-tester/vmaf-tester-report` |
| Report schema and gate | `docs/hardware-reports/report.schema.json`, `scripts/ci/check-hardware-reports.py` (in `make docs-fragments-check`) |
| Index page | `scripts/docs/generate-hardware-reports.py --write` (in `make docs-fragments-write`) |
| Workflows | `.github/workflows/docker-publish-tester.yml`, `.github/workflows/macos-tester-bundle.yml` |

One implementation serves both packages: the macOS bundle ships the same Python report
code, under a bundled interpreter, and the same schema and gate validate both reports
(HISS-19).

## Publishing, by hand

Nothing publishes on merge except a build-and-test run of the image on pushes to master.

1. **Container**: dispatch `Publish Tester Image` on `master` with exactly one of `ref`
   (a commit SHA reachable from master, or `master`, resolved to its SHA) or `tag` (a
   published release tag). The source is that commit, built with master's recipe as
   ADR-1347 does for recovery; anything not reachable from master is refused. The image
   tag is `<git describe of the commit>-tester`, for example
   `v1.0.0-rc.2-312-g1a2b3c4d-tester`. A `release: published` event does the same for
   new releases. The workflow builds both architectures on native runners, runs the
   documented `docker run` line on each, validates the report, pushes by digest, merges
   one index, signs it keyless and attests it, and publishes
   `ghcr.io/vmafx/vmafx:<tag>-tester`. The package `ghcr.io/vmafx/vmafx` is already public,
   so nothing needs a click; check once with a logged-out `docker pull`.
2. **macOS bundle**: dispatch `Publish macOS Tester Bundle` on `master` with `ref` (or
   `tag`) and `publish: true`; the file is `vmafx-tester-macos-arm64-<git describe>`. `publish: false` builds, runs the bundle's own report on the hosted
   runner and uploads a 14-day workflow artifact only. With `publish: true` the
   `release-publish` environment gate applies, then the bundle is attested, signed and
   attached to a new prerelease `tester-<date>-<sha8>` (not a product release; no other
   workflow starts, because the release is created with `GITHUB_TOKEN`).

Give the tester `<TESTER-TAG>` and `<VERSION>` (the `git describe` string) from the run summary.

## What the hosted macOS runner cannot show

The hosted runner is a virtual machine. The workflow runs the bundle's report there and
records which Metal twins ran. A runner without a usable Metal device makes `vmaf` exit
100 for `--backend metal`; the report records `no_device` (not exercised, not a
failure) and the Metal parity unit tests exit 77 (skipped). Whatever the runner cannot
exercise is first proven on the tester's machine: the Metal twins on a real GPU, the
bundle under Gatekeeper and `sandbox-exec` on his macOS version, the ad-hoc signature
on his hardware, and the interpreter on his system libraries.

## Report intake

A report PR adds one file `docs/hardware-reports/<date>-<cpu-slug>.json`. CI runs
`scripts/ci/check-hardware-reports.py` (schema, file name, integrity hash, hosted-build
facts, verdict consistency, host key allow-list). The index is not checked, so an outside
contributor need not run a generator: after merging, run `make docs-fragments-write`
and commit `docs/hardware-reports/index.md`. An outside contributor's pull request meets
the PR template's deliverables checklist; complete it on his behalf or push a commit
to his branch. For an issue submission, commit the attached file yourself with
`Co-authored-by: Name <address>` when the form gives both. No tester name belongs in
tracked files other than a commit trailer the person asked for.

## Updating the pins

| Pin | Where | Rule |
| :--- | :--- | :--- |
| Base images | `build-config.env` (`RELEASE_BUILDER_BASE`, `RELEASE_PYTHON_BASE`), mirrored in `docker/Dockerfile.tester` | `scripts/ci/check-base-image-single-source.sh --write` |
| Python test stack | `python/requirements-test-lock.txt` | `make python-locks-write` |
| Fixtures | `tools/rc1-tester/image/fixtures.sha256`, `VMAF_RESOURCE_COMMIT` | change both together; the build checks every SHA-256 |
| macOS interpreter | `PBS_URL`, `PBS_SHA256` in `macos-tester-bundle.yml` | take the hash from the release's `SHA256SUMS` |
| Unit tests | `tools/rc1-tester/image/unit-tests.txt`, `unit-tests-macos.txt` | a name absent from a build is skipped; fewer than ten found fails the build |

The Debian archive packages of the build stage are not version-pinned, as in the release
build (ADR-1346); the base image digest is.

## Local checks

```sh
python3 -m pytest -q tools/rc1-tester/tests          # report code, schema gate, bundle scripts
actionlint .github/workflows/docker-publish-tester.yml .github/workflows/macos-tester-bundle.yml
shellcheck tools/rc1-tester/image/macos/run.sh scripts/ci/check-macos-bundle-links.sh \
  scripts/ci/build-macos-tester-bundle.sh
docker build -f docker/Dockerfile.tester -t vmafx-tester:dev .
docker run --rm --network none --read-only --cap-drop ALL --security-opt no-new-privileges \
  --tmpfs /tmp vmafx-tester:dev > report.json
```

An image built this way has `built_by_workflow: false` and the CI report gate refuses it,
as intended. The x86_64 cross-architecture reference comes from the amd64 build
(`--target refs-export`); without it the `cross_arch_x86_scalar` section reads `missing`
and is informational.
