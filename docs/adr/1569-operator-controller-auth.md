<!-- markdownlint-disable MD013 MD060 -->
# ADR-1569: the operator presents a bearer token to the controller from a file it reads on every call, through credentials shared with the node

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: go, operator, controller, node, auth, security, phase4b, fork-local

## Context

The `VmafxJob` reconciler of `vmafx-operator` polls the controller's
`GetJob` for every job it tracks. It dialled with the golusoris ConnFactory's
plaintext default and sent no token, so against a controller with auth on
([ADR-0794](0794-controller-multi-tenant-auth-gateway.md), every gRPC call
authenticated and role-checked since [ADR-1518](1518-controller-grpc-authorization.md))
every poll failed with `Unauthenticated` and no `VmafxJob` ever left
`Pending`. `vmafx-node` already had a controller client with TLS and a bearer
token re-read from a file ([ADR-1524](1524-vmafx-node-controller-client.md)),
implemented inside `cmd/vmafx-node`.

A token has a lifetime, so the operator needs a source and a refresh. The
controller verifies tokens against each tenant's identity provider, and
`GetJob` reads only the caller's tenant's jobs.

## Decision

- The credential code moves out of the node into `pkg/controllerclient`:
  `Load` reads `controller.tls`, `controller.ca_file`,
  `controller.server_name`, `controller.token_file` and `controller.token`
  (env `VMAFX_CONTROLLER_*`), `Validate` refuses the combinations that cannot
  be honoured, `DialOptions` returns TLS transport credentials and per-RPC
  bearer credentials. The node and the operator both use it; there is one
  implementation.
- The token source is a file, read again on every RPC. That is the refresh:
  whatever keeps the file current (a Secret the kubelet updates in the
  mounted volume, a projected token, a sidecar that renews it from the
  identity provider) takes effect on the next poll without a restart. An
  inline `VMAFX_CONTROLLER_TOKEN` exists for tests and parity with
  `vmafx-mcp`.
- A token that is a JWT whose `exp` has passed is not sent: the call fails
  before it leaves the process with an error that names the file and the
  expiry, so a source that stopped refreshing reads as that and not as a bare
  `Unauthenticated`. A token that is not a JWT, or has no `exp`, is sent as
  is.
- The operator provides the credentials through fx from its config tree
  (`provideControllerCredentials`); a malformed combination stops it at
  startup. `VmafxJobReconciler.ControllerCredentials` feeds
  `ConnFactory.Dial`.
- The token is one tenant's: the operator tracks the `VmafxJob`s of that
  tenant. A token needs `vmafx:reader`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| File re-read per call, shared package (chosen) | One implementation with the node; any refresher works (Secret update, projected token, sidecar); no new secret material in the operator | The operator does not renew the token itself | The refresh belongs to whatever issues tokens for the cluster; the expiry check makes a stalled refresher visible |
| OAuth2 client-credentials flow inside the operator (client ID and secret, renew before expiry) | Self-contained refresh | A client secret in the operator, an identity-provider-specific token endpoint, a second token path the node does not have | Adds a credential and an implementation for one caller |
| Kubernetes service-account token verified by the controller | No external identity provider | The controller verifies tenant IdPs, not the cluster issuer; SA tokens carry no tenant claim the registry could match | Needs a new verification path in the controller |
| Copy the node's credential code into the operator | Smallest diff | Two implementations of one behaviour (HISS-19) | Refused by the reuse rule |

## Consequences

- **Positive**: `VmafxJob` status follows the controller with auth on; node
  and operator share one credential implementation and one set of variable
  names; an expired token is reported as such.
- **Negative**: one token is one tenant; `VmafxJob`s of another tenant are
  refused by the controller and stay `Pending` with the refusal in the log.
- **Neutral / follow-ups**: the Helm chart mounts the token Secret for the
  node and the operator together with the controller workload
  (`feat/helm-controller-workload`).

## References

- [ADR-0794](0794-controller-multi-tenant-auth-gateway.md), [ADR-1518](1518-controller-grpc-authorization.md),
  [ADR-1522](1522-controller-tenant-scoped-reads.md), [ADR-1524](1524-vmafx-node-controller-client.md).
- Follow-up list of 2026-10-04, Lane PLAT item 2: "the operator authenticates to the controller (token source, refresh) so its GetJob polling works with auth on".
