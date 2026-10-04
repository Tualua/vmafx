<!-- markdownlint-disable MD013 MD060 -->
# ADR-1518: The controller authorises every gRPC call against one per-method role table, and a method without an entry is refused

- **Status**: Accepted (node-API row amended by [ADR-1563](1563-controller-node-role.md))
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: security, controller, auth, grpc, go, fork-local

## Context

[ADR-0794](0794-controller-multi-tenant-auth-gateway.md) gave the
vmafx-controller three roles: `vmafx:reader` reads jobs, `vmafx:writer` also
submits and cancels them, and `vmafx:admin` also acts as a compute node
(`RegisterNode`, `Heartbeat`, `PullWork`, `ReportResult`). Only the HTTP
`POST /v1/score` route checked them. The gRPC interceptors authenticated the
token and stored the tenant, and `auth.RequireGRPCRole` existed but was called
only from tests (`cmd/vmafx-controller/auth/grpc_interceptor.go`, docs audit of
2026-10-03, defect 30). Any valid token, with any roles or none, could call
every RPC of `VmafxController` and `VmafxScoring`: a reader could submit and
cancel jobs, and any tenant's user could register as a node, pull other
tenants' jobs and report their results.

The fix has to name a role for every RPC the server serves, make a forgotten
RPC fail closed, and leave no wiring in which a call is authenticated but not
authorised.

## Decision

`auth.Config` takes a `MethodRoles` table (full method name to the roles that
may call it). `GRPCUnaryInterceptor` and `GRPCStreamInterceptor` authenticate
the call and then check the caller's roles against the method's entry in the
same function; a method without an entry is refused with `PermissionDenied`
for every caller, including the synthetic admin of `VMAFX_AUTH_DISABLED=true`.
`auth.New` rejects an entry with a malformed method name, no roles or an
unknown role, and keeps a private copy of the table.

The controller's table is `controllerMethodRoles()` in
`cmd/vmafx-controller/grpc_roles.go`, with ADR-0794's roles listed explicitly
per method (no implied hierarchy in code, as the HTTP side's
`RequireRole(writer, admin)`):

| Method | Roles |
| --- | --- |
| `VmafxScoring/Health`, `VmafxController/GetJob`, `VmafxController/StreamJobs` | reader, writer, admin |
| `VmafxScoring/Score`, `VmafxScoring/ScoreStream`, `VmafxController/SubmitJob`, `VmafxController/CancelJob` | writer, admin |
| `VmafxController/RegisterNode`, `Heartbeat`, `PullWork`, `ReportResult` | admin |

`grpc_roles_test.go` compares the table with the methods the production
server serves (`grpc.Server.GetServiceInfo`) in both directions, and calls
every RPC over the wire with a token per role against an expectation table
written out from ADR-0794 rather than read from the code.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
| --- | --- | --- | --- |
| One table enforced inside the authenticating interceptor, deny by default (chosen) | No wiring authenticates without authorising; a new RPC is refused until someone names its roles; one place to audit | Every RPC addition touches the table (the test enforces it) | — |
| Chain `RequireGRPCRole` after the auth interceptor, one instance per method behind a method router | Reuses the existing helper | Correctness depends on chain order and on the router listing every method; an RPC missing from the router is open | Fails open on omission |
| A role check at the top of each handler | Check sits next to the code it guards | Easy to forget in a new handler; nothing proves every handler has one; not deny by default | Fails open on omission |
| Role hierarchy computed in code (writer implies reader, admin implies writer) | Shorter table | A second way of reading roles beside the HTTP side's explicit lists; a reviewer has to know the hierarchy to read an entry | Explicit sets are what the HTTP route already uses |
| A dedicated node role (`vmafx:node`) instead of admin for the node API | Node credentials would not carry tenant-admin rights | A fourth role changes ADR-0794's role set and the `VmafxTenant` CRD's role enum; no node client exists yet to need it | Follow-up once the node client lands; the table makes it a one-line change |
| Exempt `VmafxScoring/Health` from authentication like HTTP `/healthz` | Unauthenticated health checks over gRPC | Kubernetes probes use the HTTP endpoints; an unauthenticated gRPC method would be the only exception in the table | Not needed by any probe |

## Consequences

- **Positive**: the role table of ADR-0794 and `docs/server/auth.md` is what
  the server enforces; a reader can no longer submit or cancel, and only an
  admin token can act as a node. An RPC added to either service without a
  role entry fails `TestEveryServedRPCHasARolePolicy` and is refused at run
  time.
- **Negative**: clients need tokens with the right role: `vmafx-mcp`'s
  `VMAFX_CONTROLLER_TOKEN` needs `vmafx:writer` to submit and cancel, and a
  node client needs `vmafx:admin`.
- **Neutral / follow-ups**: tenant scoping of `StreamJobs` and of the node
  API is a separate decision (ADR-1522); the per-tenant role whitelist of
  `VmafxTenant` is ADR-1519's.

## References

- Docs audit of 2026-10-03, defect 30: "gRPC roles unenforced: RequireGRPCRole
  only in tests (cmd/vmafx-controller/auth/grpc_interceptor.go:121); roles
  gate only HTTP POST /v1/score (main.go:324)."
- Q2026-10-04 (popup): "Track all, fix now (Recommended)"; standing rule: no
  silent fallback, deny by default.
- [ADR-0794](0794-controller-multi-tenant-auth-gateway.md): roles and threat
  model.
