# vmafx-operator

The vmafx-operator is a Kubernetes Operator built with kubebuilder v4 /
controller-runtime v0.24+. Use it to submit scoring jobs, register GPU compute
nodes and start model-training runs as Kubernetes resources. It reconciles
three VMAFX custom resource types:

| CRD | Short name | Purpose |
| --- | --- | --- |
| `VmafxJob` | `vmjob` | One reference↔distorted video-quality scoring job |
| `VmafxNode` | `vmnode` | A compute node with GPU capacity |
| `VmafxModelTraining` | `vmtrain` | Online SGD-EMA sidecar model training run |

The Helm chart ships a fourth CRD, `VmafxTenant` (short name `vmtenant`), that
the operator does not reconcile: the vmafx-controller reads it; see
[VmafxTenant CRD](#vmafxtenant-crd).

See [ADR-0714](../adr/0714-vmafx-operator-skeleton.md) for the design decision
and [ADR-0709](../adr/0709-vmafx-phase4b-distributed-platform.md) for the
broader Phase 4b context.

---

## Quick start

### Install CRDs + operator via Helm

```bash
# Clone the repository.
git clone https://github.com/VMAFx/vmafx.git && cd vmafx

# Install CRDs + operator. The image tag defaults to v<Chart.AppVersion>;
# pass --set operator.image.tag=<release tag> to pin another release.
helm upgrade --install vmafx deploy/helm/vmafx \
  --set operator.enabled=true \
  --namespace vmafx-system --create-namespace
```

CRDs are installed automatically from `deploy/helm/vmafx/crds/` on first
`helm install`.

### Verify the operator image version

The release image exposes a non-blocking version check that does not need
Kubernetes credentials or start the manager:

Images published after `v1.0.0-rc.2` have zstd layers and need Docker Engine
23.0 or later, Docker Desktop 4.19 or later, Podman or containerd 1.5 or later
([what can pull them](../usage/docker.md#what-can-pull-the-images)).

```bash
docker run --rm ghcr.io/vmafx/vmafx-operator:v1.0.0-rc.2 --version
# v1.0.0-rc.2
```

Release builds inject the published tag into `pkg/version.version`; an output
of `dev` means the image was not built by the release workflow.

### Submit a scoring job

```yaml
# job.yaml
apiVersion: vmafx.dev/v1
kind: VmafxJob
metadata:
  name: my-score-job
  namespace: vmafx-system
spec:
  reference:  "s3://my-bucket/ref.yuv"
  distorted:  "s3://my-bucket/dist.yuv"
  model:      "vmaf_v0.6.1"
  backend:    "cuda"
  priority:   10
```

```bash
kubectl apply -f job.yaml
kubectl get vmjob -n vmafx-system
# NAME            PHASE     SCORE   NODE   AGE
# my-score-job    Pending   <none>  <none> 3s
```

### Register a compute node

```yaml
# node.yaml
apiVersion: vmafx.dev/v1
kind: VmafxNode
metadata:
  name: gpu-node-0
  namespace: vmafx-system
spec:
  gpuVendor: nvidia
  capacity: 4
  image: ghcr.io/vmafx/vmafx-node:v1.0.0-rc.1  # a release tag; `latest` exists only after a final release
```

```bash
kubectl apply -f node.yaml
kubectl get vmnode -n vmafx-system
# NAME         VENDOR   HEALTHY   JOBS   DEVICE   AGE
# gpu-node-0   nvidia   true      0               12s
```

### Start a training run

```yaml
# training.yaml
apiVersion: vmafx.dev/v1
kind: VmafxModelTraining
metadata:
  name: online-training-1
  namespace: vmafx-system
spec:
  baseModel:      "vmaf_v0.6.1"
  algorithm:      "online-sgd-ema"
  outputRegistry: "ghcr.io/vmafx/models"
  dataSource:
    nodeSelector:
      gpu.vendor: nvidia
  checkpoint:
    interval:   "10m"
    minSamples: 1000
```

```bash
kubectl apply -f training.yaml
kubectl get vmtrain -n vmafx-system
# NAME               PHASE          SAMPLES   MODELVERSION   AGE
# online-training-1  Initializing   0         <none>         5s
```

---

## Architecture

The operator runs as a single Deployment (`vmafx-operator`) with a
controller-runtime Manager.  Three independent reconcilers watch their
respective CRDs.  The diagram below shows the Pod: the manager hosts the three
reconcilers and exposes metrics on `:8080` and health probes on `:8081`.

```figure
operator-reconcilers
```

---

## Helm values reference (`operator.*`)

| Key | Default | Description |
| --- | --- | --- |
| `operator.enabled` | `false` | Deploy the operator Deployment + RBAC |
| `operator.replicaCount` | `1` | Number of operator Pods |
| `operator.image.repository` | `ghcr.io/vmafx/vmafx-operator` | Image repository |
| `operator.image.tag` | `""` (→ `v<Chart.AppVersion>`) | Image tag |
| `operator.image.pullPolicy` | `IfNotPresent` | Pull policy |
| `operator.logLevel` | `info` | Log level: debug \| info \| warn \| error |
| `operator.leaderElect` | `false` | Enable leader election (requires ≥2 replicas) |
| `operator.resources` | see values.yaml | CPU/memory limits + requests |

---

## Environment variables

As of ADR-1119 Phase 1 the operator is composed with the golusoris fx
framework and is configured purely through environment variables (the previous
CLI flags are removed; fx owns signals and the run loop). Config is read from
the `operator.*` koanf subtree under the `VMAFX_` prefix.

| Variable | Default | Description |
| --- | --- | --- |
| `VMAFX_OPERATOR_METRICS_ADDR` | `:8080` | Prometheus metrics endpoint (`0` disables) |
| `VMAFX_OPERATOR_HEALTH_PROBE_ADDR` | `:8081` | Health probe endpoint |
| `VMAFX_OPERATOR_LEADER_ELECTION` | `false` | Enable leader election |
| `VMAFX_OPERATOR_LEADER_ELECTION_ID` | `vmafx-operator.vmafx.dev` | Lease name used when leader election is enabled |
| `VMAFX_OPERATOR_WEBHOOK_PORT` | `0` | Admission-webhook port; `0` disables webhooks |
| `VMAFX_OPERATOR_WEBHOOK_HOST` | _(all interfaces)_ | Admission-webhook bind host |
| `VMAFX_OPERATOR_GRACEFUL_SHUTDOWN` | `30s` | Manager graceful-shutdown timeout |
| `VMAFX_LOG_LEVEL` | `info` | Log verbosity (golusoris log module: `debug\|info\|warn\|error`) |
| `VMAFX_CONTROLLER_GRPC_ADDR` | `vmafx-controller.<ns>.svc.cluster.local:9090` | gRPC address of the vmafx-controller |
| `VMAFX_CONTROLLER_HTTP_ADDR` | `http://vmafx-controller.<ns>.svc.cluster.local:8080` | HTTP address of the vmafx-controller |
| `VMAFX_CONTROLLER_TOKEN_FILE` / `VMAFX_CONTROLLER_TOKEN` | _(none)_ | Bearer token `GetJob` sends; the file is re-read on every call ([server guide](../server/operator.md#authenticating-to-the-controller)) |
| `VMAFX_CONTROLLER_TLS` / `_CA_FILE` / `_SERVER_NAME` | `false` / _(none)_ | TLS to the controller (`pkg/controllerclient`, shared with `vmafx-node`) |

!!! warning "Migrating from the pre-fx binary (ADR-1119)"
    The CLI flags (`--metrics-bind-address`, `--health-probe-bind-address`,
    `--leader-elect`, `--log-level`, `--webhooks-enabled`) are removed.
    Update Deployment manifests and Helm values to the new variables:

| Old | New |
| --- | --- |
| `VMAFX_OPERATOR_PROBE_ADDR` | `VMAFX_OPERATOR_HEALTH_PROBE_ADDR` |
| `VMAFX_OPERATOR_LEADER_ELECT` | `VMAFX_OPERATOR_LEADER_ELECTION` |
| `VMAFX_OPERATOR_LOG_LEVEL` | `VMAFX_LOG_LEVEL` |
| `VMAFX_OPERATOR_WEBHOOKS_ENABLED` (boolean) | `VMAFX_OPERATOR_WEBHOOK_PORT` (integer) |

Set a port such as `9443` to enable webhooks; `0` or unset disables them.

---

## Running tests

### Controller envtest suite

The envtest suite installs the CRDs into an embedded etcd + API server and
verifies each reconciler's Stage 2 behaviour (14 specs).

```bash
# Install the canonical tool and envtest control-plane binaries.
make setup-envtest
eval "$(make -s setup-envtest-env)"

# Run the controller suite.
go test ./cmd/vmafx-operator/internal/controller/... -v
```

#### How the tool is pinned

The tool release and default Kubernetes generation are owned by
`SETUP_ENVTEST_VERSION` and `ENVTEST_K8S_VERSION` in `build-config.env`. Make
and CI share `scripts/ci/setup-envtest.sh`.

- The script installs the exact release into Go's `GOBIN` (or the first
  `GOPATH` entry's `bin`), checks its Go build metadata and invokes that path
  directly. A stale binary earlier on `PATH` cannot satisfy the version check.
- The selected release requires Go 1.26 or newer; the application keeps its
  own `go.mod` requirement.
- `make setup-envtest-env` never installs the tool and rejects a missing or
  mismatched version. It and the helper's `path` mode require installed assets
  and never fetch missing control-plane binaries.
- Run `make setup-envtest` first to acquire them; set
  `ENVTEST_INSTALLED_ONLY=true` on that command to require an existing offline
  asset cache.
- Override the Kubernetes version for one run with, for example,
  `make setup-envtest ENVTEST_K8S_VERSION=1.31.0`, and pass the same override
  to `setup-envtest-env`. The default is the 1.31 series.

By default the suite starts an embedded control plane. See
[the pinning evidence](../research/2058-envtest-version-owner.md).

### Webhook unit tests

Webhook validators are pure Go functions — no API server needed.

```bash
go test ./cmd/vmafx-operator/internal/webhook/... -v
```

### Run all operator tests

```bash
eval "$(make -s setup-envtest-env)"
go test ./cmd/vmafx-operator/... -v
```

---

## Webhook admission validation

Webhooks are disabled by default.  Enable by setting a webhook port, e.g.
`VMAFX_OPERATOR_WEBHOOK_PORT=9443` (and optionally
`VMAFX_OPERATOR_WEBHOOK_HOST`).  When enabled, the operator validates:

| CRD | Field | Rule |
| --- | --- | --- |
| `VmafxJob` | `spec.reference`, `spec.distorted` | Must be a valid rclone URI: non-empty, `scheme://` form |
| `VmafxNode` | `spec.gpuVendor` | Must be one of `nvidia`, `amd`, `intel`, `cpu` |

Valid URI schemes include `file://`, `s3://`, `rclone://`, `gs://`, `azure://`,
and any other `alphabet://` URI supported by rclone.

**TLS prerequisite**: the webhook server requires a valid TLS certificate.
Install
[cert-manager](https://cert-manager.io/) and annotate the webhook `Service` with
`cert-manager.io/inject-ca-from` to auto-provision the certificate.

---

## RBAC

Three minimum-permission `ClusterRole` manifests are provided:

| File | Controller | Key verbs |
| --- | --- | --- |
| `config/rbac/role_vmafxjob.yaml` | VmafxJob | get/list/watch/update/patch vmafxjobs + status |
| `config/rbac/role_vmafxnode.yaml` | VmafxNode | get/list/watch/update/patch vmafxnodes + status |
| `config/rbac/role_vmafxmodeltraining.yaml` | VmafxModelTraining | get/list/watch/update/patch vmafxmodeltrainings + status |

All three roles include `events: create/patch` (for event emission) and
`leases: *` (for leader election).  The `config/rbac/role.yaml` is the combined
aggregate used by the Helm operator RBAC template.

---

## VmafxTenant CRD

The chart also installs `VmafxTenant` (`vmtenant`, namespaced), which holds a
tenant's OIDC provider and RBAC policy for the multi-tenant auth gateway of
the vmafx-controller. The operator's `ClusterRole` grants access to it
(`deploy/helm/vmafx/templates/operator-rbac.yaml`), but the operator has no
reconciler for it: the controller lists the `VmafxTenant` resources of its
namespace itself and enforces them
([ADR-1519](../adr/1519-controller-tenant-registry.md)). The chart renders one
`VmafxTenant` per entry of `auth.tenants` when `auth.enabled` is set and
grants the controller's service account read access to them. The CRD schema
is `deploy/helm/vmafx/crds/vmafx.dev_vmafxtenants.yaml`; fields, example and
Helm values are in [server/auth.md](../server/auth.md#tenant-registry).

---

## Stage roadmap

| Stage | Status | Scope |
| --- | --- | --- |
| Stage 1 | Shipped (ADR-0714) | Skeleton, CRDs, stub reconcilers, Helm integration, envtest |
| Stage 2 | Shipped (ADR-0786) | gRPC poll loop, stale-heartbeat gate, checkpoint events, webhook validation, per-controller RBAC |
| Stage 3 | Planned | VmafxJob Pod lifecycle (create/watch/delete), controller-gen codegen CI job |
| Stage 4 | Planned | VmafxModelTraining SGD-EMA controller, checkpoint OCI push |

---

## Related documents

- [ADR-0714](../adr/0714-vmafx-operator-skeleton.md) — Stage 1 design
- [ADR-0786](../adr/0786-vmafx-operator-stage2-reconcilers.md) — Stage 2 design
- [ADR-0709](../adr/0709-vmafx-phase4b-distributed-platform.md) — Phase 4b
  platform
- [ADR-0711](../adr/0711-vmafx-controller-impl.md) — controller (sibling
  service)
- [k8s-deployment.md](k8s-deployment.md) — general k8s deployment guide
- [gpu-scheduling.md](gpu-scheduling.md) — GPU vendor scheduling
