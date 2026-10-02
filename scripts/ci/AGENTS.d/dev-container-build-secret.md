---
paths:
  - scripts/ci/check-dev-container-build-secret.py
  - scripts/ci/build-dev-container-stage.sh
  - scripts/ci/tests/test_dev_container_build_secret.py
invariant: `github_token` secret reaches exactly the stages built on `gpu-sdks`, step-scoped; never `ARG` / `ENV`; no cache flags.
---
<!-- markdownlint-disable MD013 MD060 -->
# Dev-container GitHub build secret (ADR-1271)

`check-dev-container-build-secret.py` binds these surfaces: the optional
`github_token` mount in `dev/Containerfile`, its Compose environment source,
stage builder `build-dev-container-stage.sh <target> <tag>` (ADR-1346), its
workflow callers, the GHCR publisher's secret, the NEO fetcher's rate-limit
remedy, and the anonymous/authenticated operator examples. Secret consumers
derived from stage graph (FROM parent + `COPY --from`, via
`check-container-image-references.py` parser): `build-deps` and
`release-build` consume nothing; `gpu-sdks` and every stage built on it
consume. Builder allowlists targets in a `case` (`release-build`,
`libvmaf-build`; `build-deps` dropped by ADR-1354, no caller) and forwards
`--secret` exactly for consuming targets; callers set `GITHUB_TOKEN` on the
step exactly for consuming targets, never job- or workflow-wide. Callers:
`dev-container-build.yml` (`libvmaf-build` gate + `release-build` release
rehearsal), `supply-chain.yml` `build-artifacts` (`release-build`). Never
replace the secret with `ARG` or `ENV`, make it required, or expose it to
the runtime service. Real invariant on raw builds: in those two workflows
every `docker build` is either `--check` (lint, builds no image; the gate
runs `docker build --check --target libvmaf-build`) or goes through the
builder; `dev-container-publish.yml` is the one other workflow build of
`dev/Containerfile` (build-push-action to GHCR, off release path, own
`secrets:` check). Builder stays free of `--cache-from`/`--cache-to` and
`--build-arg`: a release must not restore layers another run wrote.
`tests/test_dev_container_build_secret.py` mutation-checks those failure
modes, runs the real builder against a stub `docker`, and is wired to
pre-commit/pre-push; the Dev Container workflow additionally runs native
Docker and Compose `--check` before building.
