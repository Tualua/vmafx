<!-- markdownlint-disable MD013 MD060 -->
# ADR-1592: the controller runs under its own service account, the only one that may read VmafxTenants

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: helm, security, controller, operator, rbac, phase4b, fork-local

## Context

With a tenant registry ([ADR-1519](1519-controller-tenant-registry.md)) the
chart bound the `VmafxTenant` reader Role to the chart's single service
account, which the server, job and node pods use as well, so every one of
those pods could list the tenants' identity-provider settings. The operator's
`ClusterRole` also carried `get/list/watch` (and status updates) on
`vmafxtenants` cluster-wide, added by
[ADR-1058](1058-helm-chart-security-hardening.md) for a tenant reconciler that
does not exist: the operator registers no `VmafxTenant` type and watches no
such resource. The follow-up list of 2026-10-04 asks for separate service
accounts so that only the controller reads `VmafxTenant`s.

## Decision

- The chart creates `<serviceAccount name>-controller` whenever
  `controller.enabled` (as it creates `<name>-operator` for the operator),
  and only the controller pods run under it.
- The tenant-reader Role (namespace-scoped) is bound to that account and to
  no other. The server, job and node pods keep the shared account, which now
  holds no RBAC.
- The operator's `ClusterRole` loses its `vmafxtenants` and
  `vmafxtenants/status` rules. This replaces ADR-1058's tenant rule; the rest
  of ADR-1058 stands.
- `test_helm_service_accounts.py` resolves every Role and ClusterRole through
  its bindings and requires the accounts reaching `vmafxtenants` to be exactly
  the controller's.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Controller-only account, operator rule removed (chosen) | Least privilege: one account reads tenants; matches the code (no operator tenant reconciler) | A second account to name when `serviceAccount.create: false` users pre-create accounts | The account is created by the chart, as the operator's already is |
| One account per workload (server, node, controller) | Every pod's identity distinct | More objects; the server and node accounts would still hold no RBAC | No extra protection beyond the controller split |
| Keep the shared account, rely on the Role's namespace scope | No change | Every node and server pod reads tenant settings | Not least privilege |
| Keep the operator's tenant rule for a future reconciler | No churn | Cluster-wide read of tenant settings for a component that does not use it | Add it with the reconciler if one is ever written |

## Consequences

- **Positive**: tenant settings are readable only by the controller; the
  operator's cluster-wide grant shrinks to the resources it reconciles.
- **Negative**: deployments that pre-create service accounts must also
  provide `<name>-controller` RBAC-free names; the chart creates it.
- **Neutral / follow-ups**: none.

## References

- [ADR-1058](1058-helm-chart-security-hardening.md), [ADR-1519](1519-controller-tenant-registry.md),
  [ADR-1589](1589-helm-controller-workload.md).
- Follow-up list of 2026-10-04, Lane PLAT item 4: "separate service accounts so only the controller reads `VmafxTenant`s".
