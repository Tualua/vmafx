- **The Helm chart deploys vmafx-controller, and releases publish its image
  ([ADR-1589](docs/adr/1589-helm-controller-workload.md)).**
  `controller.enabled` renders a one-replica controller (Recreate, SQLite job
  queue on a ReadWriteOnce claim) with a Service carrying its HTTP (8080) and
  gRPC (9090) ports; the nodes and the operator are pointed at it,
  `node.controllerToken` / `operator.controllerToken` mount their bearer
  tokens from Secrets, and the NetworkPolicies open the flows between them.
  `ghcr.io/vmafx/vmafx-controller:<tag>` is built like the other Go images,
  signed, with SBOMs, licence notices and a `<tag>-source` image; the binary
  gains `--version`. **Migration:** `auth.*` now configures only the
  controller workload and needs `controller.enabled`; a release that ran a
  controller through `image.repository` fails to render and moves to
  `controller.enabled` (docs/development/k8s-deployment.md, "Upgrading to the
  controller workload").
