## Podman dev container builds `gpu-sdks` locally (2026-10-24)

`feat/devcontainer-podman-gpu`, [ADR-3520](adr/3520-podman-dev-container-local-build.md).
`.devcontainer/podman/devcontainer.json` builds the `gpu-sdks` stage of
`dev/Containerfile` by name: a rename or split of that stage changes the
`target` there in the same pull request, and the stage must keep copying no
sources, or every commit rebuilds it. The Intel NEO step reads its token from
`/run/secrets/github_token`; a sync must not bring back
`--mount=type=secret,...,env=GITHUB_TOKEN` in `dev/Containerfile`, which
Podman's Buildah refuses; `scripts/ci/check-dev-container-build-secret.py`
guards the form. The root `.devcontainer/` stays praetor's.
