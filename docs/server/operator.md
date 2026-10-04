<!-- markdownlint-disable MD060 -->
# vmafx-operator — Kubernetes Operator

`vmafx-operator` is a [kubebuilder](https://kubebuilder.io/) v4 /
controller-runtime v0.25 Kubernetes operator that watches `VmafxJob`,
`VmafxNode`, and `VmafxModelTraining` custom resources and reconciles their
Pod + status sub-resources.

The operator is built per [ADR-0714](../adr/0714-vmafx-operator-skeleton.md)
and runs as a Kubernetes `Deployment` inside the cluster.

## Quick start (in-cluster)

1. Build the operator image.

    ```bash
    docker build -f docker/Dockerfile.operator -t vmafx-operator:dev .
    ```

2. Load it into the cluster, or push it to your registry.

    ```bash
    kind load docker-image vmafx-operator:dev
    ```

3. Deploy with the Helm chart.

    ```bash
    helm upgrade --install vmafx deploy/helm/vmafx/ \
        --set operator.enabled=true \
        --set operator.image.tag=dev
    ```

## Configuration (12-factor env vars)

Runtime configuration is environment-only. `--version` is the sole process
switch and exits before the Kubernetes manager starts.

| Variable | Default | Description |
| --- | --- | --- |
| `VMAFX_OPERATOR_METRICS_ADDR` | `:8080` | Prometheus metrics endpoint bind address. |
| `VMAFX_OPERATOR_HEALTH_PROBE_ADDR` | `:8081` | Health probe endpoints (`/healthz`, `/readyz`) bind address. |
| `VMAFX_OPERATOR_LEADER_ELECTION` | `false` | Set to `true` for high-availability deployments with multiple replicas. |
| `VMAFX_OPERATOR_LEADER_ELECTION_ID` | `vmafx-operator.vmafx.dev` | Lease name used when leader election is enabled. |
| `VMAFX_OPERATOR_WEBHOOK_PORT` | `0` | Admission-webhook port; `0` disables the webhooks. |
| `VMAFX_OPERATOR_WEBHOOK_HOST` | _(all interfaces)_ | Admission-webhook bind host. |
| `VMAFX_CONTROLLER_GRPC_ADDR` | `vmafx-controller.<namespace>.svc.cluster.local:9090` | gRPC address of the controller, used by the `VmafxJob` reconciler. |
| `VMAFX_CONTROLLER_HTTP_ADDR` | `http://vmafx-controller.<namespace>.svc.cluster.local:8080` | HTTP address of the controller, used by the `VmafxNode` health probe. |
| `VMAFX_CONTROLLER_TOKEN_FILE` | _(none)_ | File holding the bearer token `GetJob` sends; read again on every call. |
| `VMAFX_CONTROLLER_TOKEN` | _(none)_ | Bearer token given inline (not together with `VMAFX_CONTROLLER_TOKEN_FILE`). |
| `VMAFX_CONTROLLER_TLS` | `false` | `true` dials the controller with TLS (system roots unless a CA file is set). |
| `VMAFX_CONTROLLER_CA_FILE` | _(none)_ | PEM bundle that verifies the controller certificate (needs `VMAFX_CONTROLLER_TLS=true`). |
| `VMAFX_CONTROLLER_SERVER_NAME` | _(none)_ | TLS server name override (needs `VMAFX_CONTROLLER_TLS=true`). |
| `VMAFX_LOG_LEVEL` | `info` | Structured log level: `debug`, `info`, `warn`, or `error`. |

See the [full environment variable reference](../usage/env-vars.md) for the
complete cross-surface table.

## Authenticating to the controller

With auth on, the controller refuses a `GetJob` without a token, and the
`VmafxJob` reconciler then logs `Failed to poll controller for job status`
on every pass and never moves the phase. Give the operator a token
([ADR-1569](../adr/1569-operator-controller-auth.md)):

- The token is a JWT the controller accepts ([Auth gateway](auth.md)): its
  tenant claim names the tenant whose jobs the `VmafxJob` resources track
  (`GetJob` reads only the caller's tenant), and its roles include
  `vmafx:reader`.
- Put it in a file and set `VMAFX_CONTROLLER_TOKEN_FILE`. The operator reads
  the file on every `GetJob`, so a token that something rewrites (a Secret
  the kubelet updates, a sidecar that renews it from the identity provider)
  applies without a restart. A JWT whose `exp` has passed is not sent; the
  poll fails with `controller token file <path> holds a token that expired at
  <time>; whatever writes it did not refresh it`.
- With `VMAFX_CONTROLLER_TLS=true` the token never travels over plaintext.

The variables are the ones `vmafx-node` reads for its own controller client
(`pkg/controllerclient`), and the operator refuses to start on a combination
it cannot honour (a token file and an inline token, a CA file without TLS).

## Health probes

| Path | Port | Purpose |
| --- | --- | --- |
| `/healthz` | `VMAFX_OPERATOR_HEALTH_PROBE_ADDR` | Liveness — returns `200 OK` when the manager is running. |
| `/readyz` | `VMAFX_OPERATOR_HEALTH_PROBE_ADDR` | Readiness — returns `200 OK` when the cache has synced. |

## Prometheus metrics

Standard controller-runtime metrics are exposed at
`VMAFX_OPERATOR_METRICS_ADDR/metrics`.  Add a `ServiceMonitor` resource to
scrape them with Prometheus Operator.

## Leader election

In a multi-replica deployment set `VMAFX_OPERATOR_LEADER_ELECTION=true`. The
operator uses a `Lease` resource named `vmafx-operator.vmafx.dev` in the
operator's namespace for the lock.

## Reconcilers

| Resource | What the reconciler does |
| --- | --- |
| `VmafxJob` | Sets the phase to `Pending`, then polls the controller's `GetJob` RPC and maps the remote status (`PENDING`, `RUNNING`, `COMPLETED`, `FAILED`, `CANCELLED`) onto the resource phase, score and timestamps. Running jobs are requeued; terminal phases are not. |
| `VmafxNode` | Probes the controller's `/healthz` every 30 s and marks the node unhealthy when the probe fails or its last heartbeat is older than 60 s. It never overwrites `status.lastHeartbeat`. |
| `VmafxModelTraining` | Sets the phase to `Initializing`, polls the sidecar trainer's `/status`, mirrors the sample count and last checkpoint into the status, and emits a `CheckpointWritten` event when a checkpoint advances. Requeues every 60 s. |

## Admission webhooks

The operator serves validating webhooks for `VmafxJob` and `VmafxNode` when
`VMAFX_OPERATOR_WEBHOOK_PORT` is set to a port other than `0`.

| Resource | Rule |
| --- | --- |
| `VmafxJob` | `spec.reference` and `spec.distorted` must be well-formed rclone URIs. The same rule applies on create and update. |
| `VmafxNode` | `spec.gpuVendor` must be one of `nvidia`, `amd`, `intel`, `cpu`. |

## Custom resource definitions

| CRD | Group | Kind |
| --- | --- | --- |
| VmafxJob | `vmafx.dev/v1` | Job submission and lifecycle tracking |
| VmafxNode | `vmafx.dev/v1` | Worker node registration and capability |
| VmafxModelTraining | `vmafx.dev/v1` | Sidecar training run lifecycle |
| VmafxTenant | `vmafx.dev/v1` | Tenant OIDC and role settings; read and enforced by the controller, not reconciled by the operator (see [auth](auth.md#tenant-registry)) |

Install CRDs from the Helm chart (enabled by default) or manually:

```bash
kubectl apply -f deploy/helm/vmafx/crds/
```

The manifests and their schemas are in `deploy/helm/vmafx/crds/`. A minimal
`VmafxJob` looks like this (see the CRD for every field):

```yaml
apiVersion: vmafx.dev/v1
kind: VmafxJob
metadata:
  name: example
spec:
  reference: s3://bucket/ref.yuv
  distorted: s3://bucket/dis.yuv
```

## References

- [ADR-0714](../adr/0714-vmafx-operator-skeleton.md) — operator design
- [ADR-0709](../adr/0709-vmafx-phase4b-distributed-platform.md) — Phase 4b
  platform
- [Kubernetes Operator
  pattern](https://kubernetes.io/docs/concepts/extend-kubernetes/operator/)
- [controller-runtime
  v0.25.2](https://pkg.go.dev/sigs.k8s.io/controller-runtime@v0.25.2)
