<!-- markdownlint-disable MD013 MD060 -->
# ADR-1522: Every job read of the controller is scoped to the caller's tenant in the query, and a node session belongs to the tenant that registered it

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: security, controller, auth, multi-tenant, grpc, go, fork-local

## Context

[ADR-0794](0794-controller-multi-tenant-auth-gateway.md) tags every job with
the submitter's tenant and scopes `GetJob` and `CancelJob` to it, and named
the `StreamJobs` filter a follow-up. The docs audit of 2026-10-03 (defect 31)
found `StreamJobs` (`cmd/vmafx-controller/grpc_server.go`) streaming every
tenant's jobs to any caller. Auditing every RPC that returns or writes jobs
found three more cross-tenant paths:

- the node API ignored tenants: a node registered with any tenant's token was
  given the oldest pending job of any tenant by `PullWork`, and its session
  token worked with any tenant's token;
- `ReportResult` wrote a result for any job ID a valid session named: a node
  could complete a job it was never given, including a pending one, with any
  score;
- `AssertTenantOwns` refused cross-tenant reads with a message naming the
  owning tenant.

[ADR-1518](1518-controller-grpc-authorization.md) limits the node API to
admins, but an admin is an admin of one tenant.

## Decision

- The queue has no read of all tenants' jobs. `Queue.ListAll` is replaced by
  `ListByTenant(ctx, tenantID, statuses)`, whose SQL carries
  `WHERE tenant_id = ?`; `PullWork` takes the tenant and skips every other
  tenant's job. `StreamJobs` reads the tenant once from the authenticated
  context and passes it into the query, so there is no window between the
  authorisation and the filter.
- A node session belongs to the tenant of the token that called
  `RegisterNode` (`nodes.Node.TenantID`). `ValidateSession` and `Heartbeat`
  compare it with the caller's tenant; `PullWork` gives the node only that
  tenant's jobs.
- `ReportResult` writes a job assigned to the reporting node (the `UPDATE`
  itself carries `AND assigned_node = ?`), or a `RUNNING` job of the
  reporter's tenant whose node has no live session: a node that registered
  again after a controller restart or an eviction reports the job it finished
  under its old session ([ADR-1524](1524-vmafx-node-controller-client.md)).
  That adoption is one compare-and-set `UPDATE` on status and tenant that
  also moves the assignment; node IDs are never reissued, so an orphaned node
  stays orphaned. Every other report (another tenant's job, a pending job, a
  live node's job, an unknown job) returns `ErrNotAssigned`
  (`PERMISSION_DENIED`) and writes nothing; a repeated report of a finished
  job is an idempotent success. A partial report is acknowledged on the same
  terms (`Queue.MayReport`).
- Ownership refusals say "resource belongs to another tenant" and name no
  tenant.
- Every handler refuses a context without a tenant (`UNAUTHENTICATED`). Jobs
  stored before the auth gateway carry the empty tenant, which no token holds.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
| --- | --- | --- | --- |
| Tenant in the SQL query of every read, tenant-bound node sessions (chosen) | No code path reads another tenant's row; nothing to forget in a handler | Shared node pools across tenants are not possible | — |
| Read all jobs and filter in the handler | Smaller queue change | Every new read path has to remember the filter; the rows are in memory either way | Fails open on omission |
| Shared node pool: any admin's node pulls any tenant's job | One pool serves all tenants | An admin of one tenant reads other tenants' job parameters and writes their results; tenant isolation ends at the node API | Cross-tenant access through the node API is the defect being fixed |
| Shared pool limited to a designated platform tenant | Shared pools for operators who want them | Needs a way to designate that tenant (the `VmafxTenant` CRD has none) and a decision on how platform nodes reach tenant data | Follow-up when a deployment needs it; deny by default until then |
| `ReportResult` only by the node the job is assigned to | Simplest rule; no node can ever write another node's job | A node that registered again (controller restart, eviction) could never report the job it finished, and ADR-1524 relies on that; the job would stay `RUNNING` | Adoption limited to orphaned `RUNNING` jobs of the same tenant keeps the cross-tenant and never-assigned refusals |
| Answer cross-tenant `GetJob` with `NOT_FOUND` instead of `PERMISSION_DENIED` | Hides whether a job ID exists | Job IDs are random UUIDs; existing clients and docs expect `PERMISSION_DENIED` | Not needed once the message names no tenant |

## Consequences

- **Positive**: a token reads, cancels and streams only its tenant's jobs; a
  node registered by one tenant never sees or completes another tenant's job;
  no node can complete a pending job or a live node's job.
- **Negative**: multi-tenant deployments need a node registration per tenant.
  Within one tenant, any node session may report a running job whose node is
  gone; the tenant's nodes trust each other.
  `Queue`, `nodes.Registry` and `scheduler.Assign` signatures gain a tenant
  (`Register`, `Heartbeat`, `ValidateSession`, `PullWork`, `ReportResult`
  gains the node); they are internal to `cmd/vmafx-controller`.
- **Neutral / follow-ups**: the `StreamJobs` push model of Phase 4b.2 keeps
  the same query. Which tenants exist and which roles they may hold is
  ADR-1519's (tenant registry).

## References

- Docs audit of 2026-10-03, defect 31: "StreamJobs no tenant filter
  (cmd/vmafx-controller/grpc_server.go:216-232): cross-tenant read."
- Lane brief 2026-10-04: "StreamJobs (and every other list / stream RPC: audit
  all of them) filtered by the caller's tenant"; "no TOCTOU between auth and
  tenant filter, deny by default".
- [ADR-0794](0794-controller-multi-tenant-auth-gateway.md),
  [ADR-0962](0962-controller-streamjobs-and-reaper-stop.md),
  [ADR-1518](1518-controller-grpc-authorization.md).
