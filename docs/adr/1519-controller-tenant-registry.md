<!-- markdownlint-disable MD013 MD060 -->
# ADR-1519: The controller reads its tenants from VmafxTenant resources (or a file of them), verifies each token against its own tenant's provider, and refuses what it cannot verify

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: security, controller, auth, multi-tenant, kubernetes, helm, go, fork-local

## Context

[ADR-0794](0794-controller-multi-tenant-auth-gateway.md) defined the
`VmafxTenant` CRD (per-tenant OIDC provider, `enabled` switch, `defaultRole`,
`allowedRoles`) and the Helm `auth.tenants` list that renders it, and left
"a reconciler reading the CRD and updating the controller's runtime config"
as a follow-up. The docs audit of 2026-10-03 (defect 32) found that no Go code
read any of it: the controller trusted one global identity provider
(`VMAFX_JWKS_ENDPOINT`, `VMAFX_AUTH_ISSUER`) and accepted any tenant name that
provider issued, and the CRD's suspension switch and role whitelist did
nothing. While checking the chart we also found that it passed the auth
settings to its default image, `vmafx-server`, which has no auth gateway, and
that `enabled: false` in `auth.tenants` rendered as `true`
(`.enabled | default true`).

## Decision

- **Source.** The controller reads the tenants itself:
  `VMAFX_AUTH_TENANTS_SOURCE=kubernetes` lists the `VmafxTenant` resources of
  its namespace through the API server (in-cluster service account), and
  `VMAFX_AUTH_TENANTS_SOURCE=file` reads a YAML or JSON stream of
  `VmafxTenant` documents (or `kubectl get -o yaml` lists) for deployments
  outside Kubernetes. Both feed one `auth.TenantRegistry`.
- **Startup is strict.** An unreadable source, an unknown field, a wrong
  `apiVersion` or `kind`, an invalid field, a `defaultRole` outside
  `allowedRoles`, a duplicated `tenantId`, a JWKS endpoint over plain HTTP to
  a non-loopback host, or a setting the chosen source does not use stops the
  controller from starting. So does combining a tenant source with
  `VMAFX_AUTH_DISABLED` or with the global provider settings (they would be
  ignored).
- **Refresh is lenient and bounded.** The source is re-read every
  `VMAFX_AUTH_TENANTS_REFRESH` (default 30 s). An invalid tenant is dropped
  (its tokens are refused) and the others reloaded; a failed read keeps the
  last set, and a set older than ten refresh intervals refuses every token
  (`UNAVAILABLE` / 503).
- **Resolution.** A token's issuer selects the tenants naming it; each
  verifies the signature with its own JWKS and the claims with its own issuer
  and audience, then reads its own tenant claim. Exactly one tenant whose
  claim value equals its `tenantId` must match, so one tenant's provider
  cannot mint tokens for another. A suspended tenant is refused with
  `PERMISSION_DENIED` / 403. Roles are the token's vmafx roles minus those not
  in `allowedRoles`; a token naming no vmafx role gets `defaultRole`.
- **Verification cost and key lifetime.** Tenants that share a JWKS endpoint
  share its key cache, and a token's signature is checked once per endpoint,
  not once per candidate tenant (a forged token naming a shared issuer would
  otherwise cost one RSA verification per tenant). Fetched keys are used for
  15 minutes and then refetched, so a key the provider withdraws stops
  verifying; when refetching fails the keys stay usable for 24 hours. Fetches
  are rate-limited whether they succeed or not, a JWKS fetch over `https`
  refuses a redirect to plain `http`, and caches no tenant names are dropped
  on reload. These apply to the single-provider mode too.
- **Refusals do not explain themselves.** An `UNAUTHENTICATED` refusal
  carries a fixed message; the reason is logged.
- **Helm.** `auth.tenants` (or `auth.tenantSource: kubernetes`) makes the
  chart pass `VMAFX_AUTH_TENANTS_SOURCE=kubernetes` and the release namespace,
  grant the workload's service account `get/list/watch` on `vmafxtenants` in
  that namespace, open the API server in the NetworkPolicy, and fill each
  rendered `VmafxTenant`'s missing `oidc` fields from the global `auth.*`
  values (which are then not passed to the controller). `enabled: false`
  renders as `false`. The chart refuses `auth.enabled` with a `vmafx-server`
  image or a non-Deployment workload, tenant settings without
  `auth.enabled`, `env.VMAFX_AUTH_*` / `env.VMAFX_JWKS_*` entries, and an
  empty `rbac.allowedRoles`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
| --- | --- | --- | --- |
| Controller lists `VmafxTenant` from the API (plus a file source outside Kubernetes) (chosen) | Helm-rendered and `kubectl`-applied tenants are read the same way; no extra component; suspension takes effect within one refresh | The controller needs read RBAC and API-server egress; two loaders | — |
| Operator reconciles `VmafxTenant` into a ConfigMap the controller mounts (ADR-0794's follow-up) | Controller needs no API access | Two components must be up for a suspension to reach the controller; kubelet ConfigMap propagation adds about a minute; operator becomes an availability dependency of auth | More moving parts, slower suspension |
| File only, rendered by Helm into a ConfigMap | Simplest controller | `VmafxTenant` resources applied with `kubectl` are never read; two sources of truth in Kubernetes | Leaves the CRD inert |
| Informer watch instead of periodic list | Changes apply in seconds | Long-lived watch, cache and resync logic in the controller for a resource that changes rarely | A 30 s list bounds the delay with far less code |
| Keep the global provider as a fallback beside the registry | Smooth migration | The global provider accepts any tenant name, which bypasses the registry's tenant list, suspension and role whitelist | Deny by default |
| On a runtime refresh keep the last good set when any tenant is invalid | One bad edit changes nothing | A bad edit next to a suspension blocks the suspension; the broken tenant keeps its old rights | Dropping only the broken tenant fails closed for it alone |
| Require `oidc.audience` for every tenant (or every tenant of a shared issuer) | Closes the confused-deputy case of tokens issued for other applications | Contradicts the CRD, which makes the field optional; the tenant claim must still match, so it is not a cross-tenant hole | A startup warning per tenant without one |
| Keep the last set forever when the source is unreachable | No outage while the API server is down | A suspension cannot take effect while the controller is cut off from the source | Bounded staleness, then refuse |

## Consequences

- **Positive**: per-tenant identity providers, suspension and role
  whitelists work as the CRD documents them; a token from one tenant's
  provider cannot act for another tenant; a typo in a tenant fails loudly.
- **Negative**: the controller fails to start on any tenant misconfiguration,
  and refuses every token if the source stays unreadable for ten refresh
  intervals (five minutes by default). A deployment with a tenant registry
  needs one entry per tenant; the global provider mode stays for
  single-provider deployments. The chart can no longer be rendered with
  `auth.enabled` on its default `vmafx-server` image.
- **Neutral / follow-ups**: the VmafxTenant read access is bound to the
  chart's shared service account, so node and job pods can read the tenant
  specs too (no secrets in them); a dedicated controller service account
  would narrow it. The chart has no published `vmafx-controller`
  image and no controller workload of its own; with `auth.enabled`,
  `image.repository` must name a controller image built from
  `docker/Dockerfile.controller` (tracked in `docs/state.md`). Writing a
  status condition back to an invalid `VmafxTenant` needs update RBAC on
  `vmafxtenants/status` and is not done; the refusal is logged.

## References

- Docs audit of 2026-10-03, defect 32: "VmafxTenant CRD / helm auth.tenants /
  allowedRoles / per-tenant OIDC read by no Go code."
- Q2026-10-04 (popup): "Implement them now" (unwired platform features: the
  tenant configuration gets implemented, not stubbed); standing rule: no
  silent fallback.
- Lane brief 2026-10-04: "a misconfigured tenant failing closed at startup".
- [ADR-0794](0794-controller-multi-tenant-auth-gateway.md),
  [ADR-1518](1518-controller-grpc-authorization.md),
  [ADR-1522](1522-controller-tenant-scoped-reads.md).
