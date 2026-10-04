<!-- markdownlint-disable MD013 MD060 -->
# ADR-1577: each tenant scores only inputs under its own scoring roots, denied by default, checked by the controller and again by the node

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: go, controller, node, security, auth, helm, phase4b, fork-local

## Context

`VmafxScoring.Score`, HTTP `POST /v1/score` and `SubmitJob` took the
reference and distorted inputs as given (`T-CONTROLLER-SCORING-PATHS-NOT-TENANT-SCOPED-2026-10-04`).
Roles ([ADR-1518](1518-controller-grpc-authorization.md)) and job tenancy
([ADR-1522](1522-controller-tenant-scoped-reads.md)) said who may call and
whose jobs they see, not which files a call may read. On storage shared
between tenants a writer of one tenant could make the controller or a node
read another tenant's media, or any file the process can open, and use the
scores as an oracle. The inputs are local paths, http(s) URLs or rclone
remotes (`pkg/storage`); `Score` reads on the controller, a job on a node.

## Decision

- **Scoring roots per tenant, deny by default.** A tenant's roots are
  absolute directories, http(s) URL prefixes and rclone remote prefixes
  (`pkg/scoringscope`, at most 32). An input is admitted only when it lies
  under one of them; a tenant without roots scores nothing.
- **Canonical comparison.** Local paths must be absolute; a `..` element is
  refused outright (also percent-encoded in URLs), then the cleaned path must
  equal the root or lie below it with a separator boundary. URLs compare
  scheme and host case-insensitively and the decoded path; rclone inputs are
  mapped to `remote:path` with `pkg/storage`'s own mapping (exported as
  `storage.RcloneRemote`) and compare the remote name and path.
- **Symlinks.** Where the files are read, the input is resolved
  (`filepath.EvalSymlinks`) and the real path must lie under a root's real
  path; the real path is what gets scored, so a final link swapped after the
  check is not followed. `Score` and `POST /v1/score` resolve on the
  controller. `SubmitJob` checks lexically (the files are on the node);
  `PullWork` hands the tenant's roots to the node in the new
  `Job.scoring_roots` (field 10, set only in `PullWork` answers), and the node
  resolves the inputs before it prepares or scores them and refuses a job
  without roots.
- **Configuration.** With a tenant registry ([ADR-1519](1519-controller-tenant-registry.md)):
  `VmafxTenant.spec.scoring.roots`, validated with the rest of the tenant.
  Without one: `VMAFX_SCORING_ROOTS`, comma-separated, `{tenant}` replaced by
  the caller's tenant ID; a tenant ID with `/`, `\`, `:` or equal to `.` or
  `..` is refused rather than substituted. Both together stop the controller.
  The chart renders `auth.scoringRoots` and each tenant's `scoring.roots` and
  refuses impossible combinations.
- Refusals are `PermissionDenied` / `403` and name the input, never another
  tenant.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Per-tenant roots, deny by default, controller and node both check (chosen) | Closes the oracle on shared storage; the node checks the files it actually opens, symlinks included | Breaking: deployments must configure roots; one `EvalSymlinks` per input | The defect is a confidentiality hole; failing closed is the safe default |
| Per-tenant roots, allow everything when none configured | No break | A forgotten setting leaves the hole open, silently | Fails open |
| Per-tenant storage credentials (each tenant its own rclone config / bucket keys) | Isolation enforced by the storage provider | Does not cover local paths or URLs; needs a credential store per tenant in controller and node | Larger change; can be added on top of roots |
| Check only on the controller | No proto change | The controller cannot see symlinks on a node's file system; a link in the tenant's own directory escapes | Leaves symlink escapes open for jobs |
| `openat2(RESOLVE_BENEATH)` and handing the vmaf CLI a file descriptor | No time-of-check window at all | Linux only, needs the CLI to read inherited descriptors, does not cover streamed inputs | Out of proportion for the tenant-to-tenant threat; the real path closes the final-link swap |

## Consequences

- **Positive**: a writer cannot read other tenants' media or arbitrary files
  through scoring; traversal and symlink escapes are refused where the files
  are read.
- **Negative**: breaking. A controller without roots refuses every input:
  set `VMAFX_SCORING_ROOTS` (or `auth.scoringRoots` in the chart) or each
  `VmafxTenant`'s `spec.scoring.roots`. A directory inside a tenant's own root
  that the tenant replaces by a symlink between the node's check and the
  CLI's open is a residual window; it needs write access to the tenant's own
  root and a race, and only the final path component is pinned.
- **Neutral / follow-ups**: the node's own `VmafxScoring` service (direct
  scoring, no tenant) is outside this rule; the chart's NetworkPolicy admits
  only the controller to it. `docs/server/auth.md` "Scoring roots",
  `controller.md`, `node.md` and the env reference document the rule.

## References

- [ADR-1518](1518-controller-grpc-authorization.md), [ADR-1519](1519-controller-tenant-registry.md),
  [ADR-1522](1522-controller-tenant-scoped-reads.md), [ADR-1526](1526-node-storage-streamed-inputs.md).
- `T-CONTROLLER-SCORING-PATHS-NOT-TENANT-SCOPED-2026-10-04` (security review of the Lane SEC diff).
- Follow-up list of 2026-10-04, Lane PLAT item 5: "scoring paths scoped per tenant (a writer can only score paths under its tenant's configured roots; deny by default; tests for traversal and symlink escapes)".
