<!-- markdownlint-disable MD013 MD060 -->
# Verifying the release and tester workflows before they publish

Six workflows publish something and used to start only after a merge or a
release: the tester image, the Windows zip, the macOS bundle, the production
images, the operator / server / node images and the supply-chain run. A change
to their inputs was first built by the merge or the release itself. Each of them
now has a pull-request or scheduled verify that builds and runs the same
targets without publishing. The choice per workflow is recorded in
[ADR-1595](../adr/1595-pr-time-verify-push-only-workflows.md).

## When you need this page

A pull request shows a red `Publish Tester Image`, `Publish Windows Tester Bundle`
or `Release Dry Run` check, or you change a Dockerfile, a toolkit pin
(`build-config.env`), the licence inputs (`tools/rc1-tester/image/`) or the
`vmaf-mcp` package and want to know which build will exercise it.

## What runs where

| Workflow | Pull request | Weekly | Left to the merge or the release |
| --- | --- | --- | --- |
| `docker-publish-tester.yml` | `Build and test (amd64)` and the x86_64 reference scores, when the push path list matches | none | arm64 image; Intel, NVIDIA and AMD GPU images; publishing |
| `windows-tester-bundle.yml` | the x64 zip: build, run, verify, SBOM, when the push path list matches | none | arm64, x64 CUDA and x64 SYCL zips; publishing |
| `macos-tester-bundle.yml` | none (macOS minutes cost ten times a Linux minute) | Monday 04:23 UTC: build and verify master's head | publishing |
| `docker-publish-production.yml` | `Release Dry Run`: the CPU CLI image and the MCP server image (linux/amd64, `--version`, a score with the built-in model); the CUDA 13, ROCm 10 and oneAPI 2026 images built, not run | Wednesday 03:41 UTC: all of it | arm64; HTTP startup; signing; attestation; publishing |
| `docker-publish-operator-node.yml` | `Release Dry Run`: the operator, vmafx-server and vmafx-node images (linux/amd64, `--version` equals the stamped tag, a score, the node's ffmpeg and rclone) | Wednesday 03:41 UTC: all of it | arm64; HTTP startup; the multi-arch manifest; signing; publishing |
| `supply-chain.yml` | `Release Dry Run`: the `vmaf-mcp` wheel and sdist named for the release version, the SBOMs of the installed package and the check of those SBOMs against the files | Wednesday 03:41 UTC | native artifacts (rehearsed by `dev-container-build.yml`, [ADR-0819](../adr/0819-dev-container-ci-gate.md)); signing; provenance; the PyPI publication |

Nothing in a pull-request or scheduled run logs in to a registry, pushes, signs,
attests or creates a release; `scripts/ci/tests/test_pr_time_verify_workflows.py`
fails if `release-dry-run.yml` gains any of those.

## Which dry run a pull request gets

`Release Dry Run` always starts and its `Plan release dry run` job decides, from
the changed paths, which of three groups to run. [`scripts/ci/release-dry-run-plan.sh`](../../scripts/ci/release-dry-run-plan.sh)
holds the lists:

| Group | Runs when the diff touches |
| --- | --- |
| images | `docker/Dockerfile.production`, `docker/Dockerfile.operator`, `docker/Dockerfile.node`, `Dockerfile.go-server`, `ffmpeg-patches/`, `build-config.env`, the licence inputs (`tools/rc1-tester/image/`, `REUSE.toml`, `LICENSES/`), the install scripts the images copy, the two publishing workflows, the dry run itself |
| gpu | `docker/Dockerfile.production-gpu`, the CUDA, ROCm and oneAPI install scripts, `build-config.env`, the licence inputs, the production workflow, the dry run itself |
| mcp | `mcp-server/vmaf-mcp/`, `requirements/locks/package-build.txt`, `scripts/release/verify-mcp-sbom.sh`, `scripts/release/pep440-version.sh`, `supply-chain.yml`, the dry run itself |

A group that is off is not exercised by that pull request; the step summary of
the plan job says so. The `docker-publish-tester.yml` and `windows-tester-bundle.yml` runs
use a `paths:` filter of their own (the list of their `push` trigger).

## Reproducing a failure locally

The image dry run is `docker build` of the same file and target, then the smoke
script:

```bash
docker build --file docker/Dockerfile.production --target cli \
  --build-arg VMAFX_VERSION=dev --tag release-dry-run:cli .
bash scripts/ci/release-image-smoke.sh release-dry-run:cli vmaf dev
```

The kinds are `vmaf` (the CLI image), `mcp`, `wrapped` (vmafx-server), `node` and
`binary` (the operator); the header of the script says what each runs. The
`vmaf-mcp` SBOM check is `bash scripts/release/verify-mcp-sbom.sh <version> <sbom-dir> <dist-dir>`,
the script `supply-chain.yml` runs on a release. Its fixtures:
`bash scripts/release/tests/test-verify-mcp-sbom.sh`.

## Changing a release workflow

The dry run builds what the release builds. A new image target, a new
`vmaf-mcp` build command or a new Syft pin goes into both
`release-dry-run.yml` and the release workflow, or
`scripts/ci/tests/test_pr_time_verify_workflows.py` fails.
