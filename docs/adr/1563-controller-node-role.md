<!-- markdownlint-disable MD013 MD060 -->
# ADR-1563: a dedicated vmafx:node role is the only role that reaches the controller's node API

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: go, controller, node, security, auth, phase4b, fork-local

## Context

[ADR-1518](1518-controller-grpc-authorization.md) gave every controller gRPC
method a role list and put the node API (`RegisterNode`, `Heartbeat`,
`PullWork`, `ReportResult`) under `vmafx:admin`, the role that also reads,
submits and cancels jobs and scores directly. A compute node therefore had to
hold the broadest role there is: a node's credential, which sits on every GPU
host and in every node pod, could read and cancel every job of its tenant and
make the controller score arbitrary paths, and any administrator token could
register a node and take jobs off the queue. The node needs four calls and no
others. The follow-up list of 2026-10-04 asks for a dedicated `vmafx:node` role
in the `VmafxTenant` role list and the gRPC role table instead of admin.

## Decision

- `vmafx:node` (`auth.RoleNode`) is a fourth role. `controllerMethodRoles()`
  lists it, and only it, for the four node-API methods; it appears in no other
  entry. `vmafx:admin` keeps every reader and writer call and loses the node
  API.
- `auth.IsKnownRole` accepts it, so tenant registries can allow it. The
  `VmafxTenant` CRD offers it in `rbac.allowedRoles` (not in the default
  list). `rbac.defaultRole: vmafx:node` is not offered by the CRD and the
  controller refuses it from any source: the node role is granted by a token's
  own claim, never to a token that names no role.
- The synthetic caller of `VMAFX_AUTH_DISABLED=true` holds `vmafx:admin` and
  `vmafx:node`, so a node of a disabled-mode controller still registers.
- `TestGRPCRolesEnforcedPerRPC` holds the new expectation table independently
  of the code: admin is refused on the node API, a node token is refused
  everywhere else and admitted on the node API.

This changes ADR-1518's node row only; its table mechanism and deny-by-default
rule stand.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| `vmafx:node` only (chosen) | Least privilege in both directions: a node credential reaches four calls, a user credential never acts as a node | Breaking for deployments whose nodes use admin tokens | The brief asks for a node role instead of admin; the break is announced with a migration |
| `vmafx:node` or `vmafx:admin` on the node API | No break for existing node tokens | Admin tokens keep the ability to register nodes and pull jobs; the separation holds in one direction only | Leaves the over-broad grant that motivated the change |
| Keep admin, document narrower tokens | No code change | Roles cannot express "node only"; any narrowing lives outside the controller | Not enforceable |
| Allow `defaultRole: vmafx:node` | Symmetric enums | Every role-less token of such a tenant becomes a node | Deny by default: a node needs the claim |

## Consequences

- **Positive**: a stolen node token can no longer read, submit or cancel jobs
  or score; an administrator token can no longer pose as a node.
- **Negative**: breaking change for running deployments. Node tokens must
  carry `vmafx:node` instead of `vmafx:admin`, and every registry tenant that
  runs nodes must list `vmafx:node` in `allowedRoles`; until then its nodes
  are refused with `role required: vmafx:node`.
- **Neutral / follow-ups**: `docs/server/auth.md`, `docs/server/node.md`, the
  CRD and the chart's examples name the new role. The Helm wiring of node
  tokens follows with the controller workload.

## References

- [ADR-0794](0794-controller-multi-tenant-auth-gateway.md), [ADR-1518](1518-controller-grpc-authorization.md),
  [ADR-1519](1519-controller-tenant-registry.md), [ADR-1524](1524-vmafx-node-controller-client.md).
- Follow-up list of 2026-10-04, Lane PLAT item 3: "a dedicated node role (`vmafx:node`) in the CRD's role list and the gRPC role table instead of admin for the node API".
