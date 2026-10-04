# vmafx-controller: Multi-Tenant Auth Gateway

> ADR: [ADR-0794](../adr/0794-controller-multi-tenant-auth-gateway.md)

The vmafx-controller supports multi-tenant deployments through a built-in
JWT auth gateway. You configure it with `VMAFX_*` environment variables (the
controller has no CLI flags beyond `--version`, since ADR-1119) and send a
bearer token on every request.

Every gRPC and HTTP request, except the liveness, readiness and metrics
endpoints, must carry a valid RS256 bearer token from a configured OIDC
provider. Requests are scoped to the tenant identified by the token, and every
gRPC call and the HTTP `POST /v1/score` endpoint require a role from the token;
see [Roles and RBAC](#roles-and-rbac).

## Table of contents

- [Quick start](#quick-start)
- [Token structure](#token-structure)
- [OIDC provider configuration](#oidc-provider-configuration)
  - [Auth0](#auth0)
  - [Keycloak](#keycloak)
  - [Dex](#dex)
- [Roles and RBAC](#roles-and-rbac)
- [Tenant isolation](#tenant-isolation)
- [Helm configuration](#helm-configuration)
- [VmafxTenant CRD](#vmafxtenant-crd)
- [Disabling auth](#disabling-auth)
- [Environment variables](#environment-variables)
- [Key rotation](#key-rotation)
- [Threat model summary](#threat-model-summary)

---

## Quick start

1. Start the controller with auth enabled (Auth0 example).

    ```bash
    VMAFX_JWKS_ENDPOINT=https://YOUR_DOMAIN.auth0.com/.well-known/jwks.json \
    VMAFX_AUTH_ISSUER=https://YOUR_DOMAIN.auth0.com/ \
    VMAFX_AUTH_AUDIENCE=https://vmafx.example.com/api \
      vmafx-controller
    ```

2. Fetch a token from the identity provider.

    ```bash
    TOKEN=$(curl -s -X POST \
      https://YOUR_DOMAIN.auth0.com/oauth/token \
      -d grant_type=client_credentials \
      -d client_id=YOUR_CLIENT_ID \
      -d client_secret=YOUR_CLIENT_SECRET \
      -d audience=https://vmafx.example.com/api \
      | jq -r .access_token)
    ```

3. Call the API with the bearer token.

    ```bash
    curl -H "Authorization: Bearer $TOKEN" \
      http://localhost:8080/v1/score \
      -d '{"reference":"/data/ref.yuv","distorted":"/data/dist.yuv"}'
    ```

---

## Token structure

The controller extracts the following claims from the JWT payload:

| Claim | Required | Default field | Description |
| --- | --- | --- | --- |
| `iss` | Yes | — | Must match `VMAFX_AUTH_ISSUER`. |
| `exp` | Yes | — | Token expiry; checked on every request. |
| `aud` | No | — | Checked if `VMAFX_AUTH_AUDIENCE` is set. |
| `sub` | No | — | Subject (logged for audit). |
| `tid` | Yes* | `VMAFX_AUTH_TENANT_CLAIM` | Tenant identifier. |
| `vmafx_roles` | No | `VMAFX_AUTH_ROLES_CLAIM` | List of role strings. |

*`tid` is required unless `VMAFX_AUTH_DISABLED=true` is set.

Example payload:

```json
{
  "iss": "https://idp.example.com/",
  "sub": "user|abc123",
  "aud": "https://vmafx.example.com/api",
  "exp": 1893456000,
  "tid": "acme",
  "vmafx_roles": ["vmafx:writer"]
}
```

---

## OIDC provider configuration

The controller only needs the IdP's JWKS endpoint and issuer URL. It does
not perform OIDC discovery automatically; provide the endpoint directly.

Rules for the signing keys in the JWKS:

- Keys must be RSA keys of at least 2048 bits with an odd public exponent
  (normally 65537).
- A key below 2048 bits is skipped and logged as
  `jwks: skipping RSA key below the minimum size` with its `kid`. Tokens
  signed with that key are rejected with `401`; other keys in the same JWKS
  keep working.
- A key with a malformed exponent makes the JWKS refresh fail.

### Auth0

```bash
VMAFX_JWKS_ENDPOINT=https://YOUR_DOMAIN.auth0.com/.well-known/jwks.json
VMAFX_AUTH_ISSUER=https://YOUR_DOMAIN.auth0.com/
VMAFX_AUTH_AUDIENCE=https://vmafx.example.com/api
VMAFX_AUTH_TENANT_CLAIM=org_id       # Auth0 organisation ID claim
```

In Auth0, add the `org_id` claim to your token and create a custom
`vmafx_roles` action in the Auth0 Login flow.

### Keycloak

```bash
VMAFX_JWKS_ENDPOINT=https://keycloak.example.com/realms/vmafx/protocol/openid-connect/certs
VMAFX_AUTH_ISSUER=https://keycloak.example.com/realms/vmafx
VMAFX_AUTH_AUDIENCE=vmafx-api
VMAFX_AUTH_TENANT_CLAIM=tid          # add as a custom mapper in Keycloak
VMAFX_AUTH_ROLES_CLAIM=vmafx_roles   # add as a custom mapper in Keycloak
```

### Dex

```bash
VMAFX_JWKS_ENDPOINT=https://dex.example.com/keys
VMAFX_AUTH_ISSUER=https://dex.example.com
VMAFX_AUTH_TENANT_CLAIM=tid
```

---

## Roles and RBAC

Three roles are recognised. Include one or more in the `vmafx_roles` claim (a
JSON array, or a single string):

| Role | May call |
| --- | --- |
| `vmafx:reader` | `GetJob`, `StreamJobs`, `VmafxScoring.Health` |
| `vmafx:writer` | Everything a reader may, plus `SubmitJob`, `CancelJob`, `VmafxScoring.Score`, `VmafxScoring.ScoreStream` and HTTP `POST /v1/score` |
| `vmafx:admin` | Everything a writer may, plus the node API: `RegisterNode`, `Heartbeat`, `PullWork`, `ReportResult` |

The controller enforces this table on every call
([ADR-1518](../adr/1518-controller-grpc-authorization.md)):

- A gRPC call whose token holds none of the method's roles fails with
  `PERMISSION_DENIED` and the message `role required: <roles>` before the
  handler runs. A token without a `vmafx_roles` claim, with an empty one, or
  with only unknown strings (for example `vmafx:root`) holds no role and is
  refused everywhere.
- HTTP `POST /v1/score` answers `403 Forbidden` to a token without
  `vmafx:writer` or `vmafx:admin`.
- A gRPC method the controller does not list in its role table is refused for
  every caller, including the synthetic admin of [disabled
  mode](#disabling-auth). The table lives in
  `cmd/vmafx-controller/grpc_roles.go`, and a test fails when a served method
  is missing from it.

Roles are not inherited implicitly: the table above lists, for each call,
every role that may make it. A writer can read because reader calls also list
`vmafx:writer`.

Clients therefore need a token with the right role:

- `vmafx-mcp` sends `VMAFX_CONTROLLER_TOKEN` with every controller call; its
  `submit_job` and `cancel_job` tools need `vmafx:writer`, `get_job` and
  `list_jobs` need `vmafx:reader`.
- A compute node needs `vmafx:admin`.

---

## Tenant isolation

Every job is tagged with the `tenant_id` of the submitter's token, and every
call reads and writes only the jobs of its own token's tenant
([ADR-1522](../adr/1522-controller-tenant-scoped-reads.md)):

- `SubmitJob` stamps the new job with the caller's tenant.
- `GetJob` and `CancelJob` answer `PERMISSION_DENIED` (`resource belongs to
  another tenant`) for another tenant's job, without naming that tenant.
- `StreamJobs` streams only the caller's tenant's jobs; the tenant is part of
  the database query, so no other tenant's job is read.
- A node session belongs to the tenant of the token that called
  `RegisterNode`. `Heartbeat` answers `ok=false`, and `PullWork` and
  `ReportResult` answer `PERMISSION_DENIED`, when called with that session and
  a token of another tenant. `PullWork` gives a node only its tenant's jobs.
- `ReportResult` accepts a result, final or partial, for a job assigned to the
  reporting node, or for a running job of the same tenant whose node has no
  live session any more (a node that registered again after a controller
  restart or an eviction reports what it finished before). A report for a job
  of another tenant, a pending job, a job of a live node or an unknown job
  answers `PERMISSION_DENIED` (`job "<id>" is not assigned to node "<id>"`)
  and changes nothing. Repeating a final report of a finished job succeeds
  without changing it.

A deployment that serves several tenants from one pool of nodes therefore
needs a node registration per tenant; nodes shared across tenants are not
supported. Jobs stored before the auth gateway carry the empty tenant, which
no token can hold, so no caller can read them.

Tenant IDs are opaque strings compared exactly (case and whitespace count).

---

## Helm configuration

```yaml
auth:
  enabled: true
  jwksEndpoint: https://idp.example.com/.well-known/jwks.json
  issuer: https://idp.example.com/
  audience: vmafx-api          # optional
  tenantClaim: tid             # default
  rolesClaim: vmafx_roles      # default

  tenants:
    - tenantId: acme
      oidc:
        issuer: https://acme.auth0.com/
        jwksEndpoint: https://acme.auth0.com/.well-known/jwks.json
        audience: vmafx-api
      rbac:
        defaultRole: vmafx:reader
        allowedRoles: [vmafx:reader, vmafx:writer]
```

The `auth.tenants` list creates `VmafxTenant` CRs in the same namespace. The
other `auth.*` values become the `VMAFX_*` environment variables of the
[environment table](#environment-variables).

---

## VmafxTenant CRD

Each tenant can be configured as a Kubernetes custom resource:

```yaml
apiVersion: vmafx.dev/v1
kind: VmafxTenant
metadata:
  name: acme
spec:
  tenantId: acme
  enabled: true
  oidc:
    issuer: https://acme.auth0.com/
    jwksEndpoint: https://acme.auth0.com/.well-known/jwks.json
    audience: vmafx-api
    tenantClaim: org_id
    rolesClaim: vmafx_roles
  rbac:
    defaultRole: vmafx:reader
    allowedRoles: [vmafx:reader, vmafx:writer]
```

`kubectl apply` VmafxTenant CRs directly for operator-managed multi-tenant
clusters. The CRD is installed by the Helm chart's `crds/` directory.

!!! note "Which configuration wins"
    The Helm `auth.tenants` list and a hand-written `VmafxTenant` are two ways
    to create the same resource. The controller itself reads only the global
    `VMAFX_*` variables. No binary in this repository reads `VmafxTenant`
    resources yet (the operator has no reconciler for them), so per-tenant
    `oidc` and `rbac` settings, including `allowedRoles`, are stored but not
    applied.

---

## Disabling auth

For internal deployments or integration-test pipelines:

```bash
VMAFX_AUTH_DISABLED=true vmafx-controller
```

When disabled, all requests are processed as tenant `dev` with role
`vmafx:admin`. Never use this in production.

---

## Environment variables

The controller has no CLI flags beyond `--version`; all configuration is
environment-only (ADR-1119).

| Env var | Default | Description |
| --- | --- | --- |
| `VMAFX_AUTH_DISABLED` | `false` | Bypass all auth checks. |
| `VMAFX_JWKS_ENDPOINT` | — | JWKS endpoint URL. |
| `VMAFX_AUTH_ISSUER` | — | Expected `iss` claim value. |
| `VMAFX_AUTH_AUDIENCE` | — | Expected `aud` claim value. |
| `VMAFX_AUTH_TENANT_CLAIM` | `tid` | Tenant claim field name. |
| `VMAFX_AUTH_ROLES_CLAIM` | `vmafx_roles` | Roles claim field name. |

The listen addresses and the other controller settings are in
[controller.md](controller.md#configuration).

---

## Key rotation

When the controller receives a token whose `kid` (key ID) is not in the
local JWKS cache, it fetches the JWKS endpoint once. To prevent thundering-
herd on rotation, re-fetches are rate-limited to one per 30 seconds.

If the new key is not present in the endpoint's response within the cooldown
window, requests with the new `kid` are rejected with `401` until the cache
refreshes successfully.

---

## Threat model summary

| Threat | Mitigation |
| --- | --- |
| Algorithm confusion (`alg=none`, `alg=HS256`) | Only RS256 is accepted; any other `alg` header is rejected before key lookup. |
| Token replay | `exp` checked on every request. |
| Cross-tenant data access | Every job read and write is scoped to the token's tenant (`GetJob`, `CancelJob`, `StreamJobs`, `SubmitJob`); node sessions belong to one tenant, nodes pull only that tenant's jobs and report only jobs assigned to them. Refusals do not name the owning tenant. |
| JWKS endpoint spoofing | Endpoint configured by operator via trusted Helm/env values. |
| Privilege escalation | Every gRPC method and HTTP `POST /v1/score` require a role from the token; a gRPC method without a role entry is refused. The `allowedRoles` whitelist of VmafxTenant is not applied yet: no component reads it. |
| Revocation | Use short-lived tokens (≤1 hour); revocation list support is a follow-up. |
