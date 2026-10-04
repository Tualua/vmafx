# vmafx-controller: Multi-Tenant Auth Gateway

> ADR: [ADR-0794](../adr/0794-controller-multi-tenant-auth-gateway.md)

The vmafx-controller supports multi-tenant deployments through a built-in
JWT auth gateway. You configure it with `VMAFX_*` environment variables (the
controller has no CLI flags beyond `--version`, since ADR-1119) and send a
bearer token on every request.

Every gRPC and HTTP request, except the liveness, readiness and metrics
endpoints, must carry a valid RS256 bearer token from a configured OIDC
provider. Requests are scoped to the tenant identified by the token. Roles
embedded in the token gate only the HTTP `POST /v1/score` endpoint today; see
[Roles and RBAC](#roles-and-rbac) for what is and is not enforced.

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

Three roles are recognised. Include one or more in the `vmafx_roles` claim:

| Role | Intended operations |
| --- | --- |
| `vmafx:reader` | `GetJob`, `StreamJobs`, health endpoints |
| `vmafx:writer` | All of reader + `SubmitJob`, `CancelJob`, `POST /v1/score` |
| `vmafx:admin` | All of writer + `RegisterNode`, `Heartbeat`, `PullWork`, `ReportResult` |

### What is enforced today

Roles gate only one endpoint: HTTP `POST /v1/score` requires `vmafx:writer`
or `vmafx:admin`. A token with no `vmafx_roles` claim, or an empty one, is
rejected with `403 Forbidden` there.

The gRPC API authenticates the token and records the tenant, but it does not
check roles. Any valid token can call every RPC, including `RegisterNode`,
`Heartbeat`, `PullWork` and `ReportResult`. What gRPC enforces is tenant
ownership of `GetJob` and `CancelJob` (see [Tenant
isolation](#tenant-isolation)).

!!! warning "Known gaps"
    Treat the role table above as the intended model, not as a guarantee, until
    the gRPC role checks land. A gRPC client with any valid token can also
    register as a node and pull work.

---

## Tenant isolation

Every job is tagged with the `tenant_id` extracted from the submitter's
token at submission time. The controller enforces:

- `GetJob` / `CancelJob` — returns `PERMISSION_DENIED` if the caller's
  `tenant_id` does not match the job's stored tenant.
- `SubmitJob` — stamps the new job with the caller's `tenant_id`.

`StreamJobs` is not scoped: it streams a snapshot of all jobs matching the
optional status filter, regardless of tenant. Tenant-scoped filtering is
planned for Phase 4b.2.

!!! warning "Known gap"
    `StreamJobs` lets any authenticated caller read the jobs of every
    tenant.

Tenant IDs are opaque strings; the controller does not interpret them beyond
equality comparison.

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
| Cross-tenant data access | `tenant_id` ownership enforced on `GetJob`, `CancelJob` and `SubmitJob`. `StreamJobs` is not tenant-scoped. |
| JWKS endpoint spoofing | Endpoint configured by operator via trusted Helm/env values. |
| Privilege escalation | Not mitigated by the `allowedRoles` whitelist of VmafxTenant yet: no component applies it. HTTP `POST /v1/score` requires a writer or admin role; gRPC does not check roles. |
| Revocation | Use short-lived tokens (≤1 hour); revocation list support is a follow-up. |
