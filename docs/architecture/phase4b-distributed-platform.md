<!-- markdownlint-disable MD013 MD060 -->
# VMAFX Phase 4b — Distributed Platform Architecture

This page shows how the controller, node and operator binaries fit together
on a Kubernetes cluster, and how each one is configured. Read the diagram
and the responsibilities table first; the storage, GPU affinity and
implementation-status sections follow.

!!! note "Status"
    The controller, node, operator and server binaries are implemented under
    `cmd/` and documented under [docs/server/](../server/controller.md). The
    umbrella [ADR-0709](../adr/0709-vmafx-phase4b-distributed-platform.md)
    itself still carries the status `Proposed`. The
    [implementation status](#implementation-status) table lists each sweep
    step.

The layout replaces the single-binary model from Phase 3 (ADR-0701) and
Phase 4a (ADR-0702) with a controller/node/operator split designed for
horizontal scale on heterogeneous GPU clusters. `vmafx-server` is the
Phase 4a single-binary scoring service; it remains a separate binary (see
[gRPC server](../server/grpc.md)).

## Component diagram

```figure
phase4b-platform
```

## Component responsibilities

| Component | Language | Image | Key responsibilities |
| --- | --- | --- | --- |
| `vmafx-controller` | Go | distroless/cc | gRPC + HTTP API, job queue, node registry, scheduler, `/healthz /readyz /metrics` |
| `vmafx-operator` | Go (controller-runtime) | distroless/cc | Watches `VmafxJob` / `VmafxNode` / `VmafxModelTraining` CRDs (`VmafxTenant` ships as a CRD without a reconciler); reconciles pod lifecycle; drives HPA |
| `vmafx-node` | Go | distroless/cc + ffmpeg + rclone | Pulls work, runs ffmpeg subprocess, scores via libvmaf cgo, AI inference via Go ONNX Runtime, captures training triples |
| `training-sidecar` | Python (PyTorch + Lightning) | pytorch base | Consumes `(ref, dis, score, metadata)` triples from co-located node; continuously fine-tunes ONNX model; writes updated `.onnx` to model registry |
| `vmafx-mcp` | Go | distroless/cc | MCP JSON-RPC server; 5 of its 24 tools (`submit_job`, `get_job`, `cancel_job`, `list_jobs`, `vmaf_score_remote`) call the controller over gRPC, the others run the `vmaf` CLI directly (see [MCP tools](../mcp/tools.md)) |
| `vmafx-server` | Go | distroless/cc | Phase 4a single-binary scoring service: gRPC `VmafxScoring` + REST (see [gRPC server](../server/grpc.md), [REST adapter](../server/rest.md)) |
| `vmafx-tune` | Go | distroless/cc | Encoder-ladder optimizer; submits jobs to controller |

## CRD summary

All four CRDs belong to the group `vmafx.dev`, version `v1`
(`deploy/helm/vmafx/crds/`).

| CRD | Group / version | Scope | Purpose |
| --- | --- | --- | --- |
| `VmafxJob` | `vmafx.dev/v1` | Namespaced | Describes a scoring / encoding / QA job (source, models, encoder params, target node pool) |
| `VmafxNode` | `vmafx.dev/v1` | Namespaced | Describes a node pool (GPU vendor, count, image, resource limits) |
| `VmafxModelTraining` | `vmafx.dev/v1` | Namespaced | Describes a sidecar training run (base model, training config, output target) |
| `VmafxTenant` | `vmafx.dev/v1` | Namespaced | Maps an auth tenant to its settings (see [auth](../server/auth.md)); no reconciler in the operator |

## Storage flow (zero-copy via rclone)

```text
Object store (S3 / GCS / Azure Blob / SFTP)
        │
        │  rclone mount / rclone-vfs (FUSE)
        ▼
  /mnt/source/   (inside vmafx-node pod)
        │
        │  POSIX read — no intermediate disk write
        ├──► ffmpeg subprocess (encode → encoded stream)
        └──► libvmaf cgo (score → result JSON)
```

## GPU pool affinity

Node pods are scheduled via k8s `nodeSelector` / `nodeAffinity` resource keys:

| Vendor | Resource key | Backend |
| --- | --- | --- |
| NVIDIA | `nvidia.com/gpu` | CUDA EP |
| AMD | `amd.com/gpu` | ROCm EP + HIP |
| Intel | `gpu.intel.com/i915` | OpenVINO EP + SYCL |

Each backend runs through whichever GPU device plugin is allocated to the pod
(per ADR-0701). (The Vulkan backend was removed in ADR-0726.)

## Implementation status

The nine sweep steps of ADR-0709 and their state in the tree. A step counts
as done when its code or artefact exists on `master`.

| Phase | Description | Input dependency | State |
| --- | --- | --- | --- |
| 4b.1 | `vmafx-server` → `vmafx-controller` (job queue, node registry, scheduler) | Phase 4a vmafx-server PR merged | Done (`cmd/vmafx-controller`) |
| 4b.2 | `vmafx-node` Go binary (libvmaf cgo, ffmpeg, Go ONNX Runtime) | vmafx-sys Rust bindings (Phase 4a) | Done (`cmd/vmafx-node`, ADR-0713) |
| 4b.3 | `vmafx-operator` kubebuilder skeleton + CRDs | Phase 4b.1 | Done (`cmd/vmafx-operator`) |
| 4b.4 | ffmpeg latest + `ffmpeg-patches/` bundled in node image | Phase 4b.2 | Done (`docker/Dockerfile.node`, ADR-0717) |
| 4b.5 | rclone integration (node distroless layer + mount lifecycle) | Phase 4b.2 | Done (rclone stage in `docker/Dockerfile.node`) |
| 4b.6 | eBPF research digest + ONE concrete optimization | Phase 4b.2 (baseline measurement) | Research done ([Research-0733](../research/0733-vmafx-ebpf-optimization-target.md)); probe code under `cmd/vmafx-node/bpf` |
| 4b.7 | Sidecar training v1 (Python sidecar + triple-capture API) | Phase 4b.2 + Phase 4b.3 | Helm template present (`sidecar-trainer.yaml`) |
| 4b.8 | C ABI break + ffmpeg-patches update | Phase 4b.4 | Open; tracked by ADR-0709 |
| 4b.9 | Native build sunset (Docker + Helm only release artifacts) | Phase 4b.8 | Open; tracked by ADR-0709 |

## Related documents

- [ADR-0709](../adr/0709-vmafx-phase4b-distributed-platform.md) — umbrella
  decision record
- [ADR-0702](../adr/0702-vmafx-phase4-language-modernization.md) — Phase 4a
  foundation
- [ADR-0701](../adr/0701-vmafx-cloud-native-redesign.md) — Phase 3 cloud-native
  redesign
- [ADR-0699](../adr/0699-vmafx-helm-chart-k8s.md) — Helm chart + k8s manifests
- [c4-container.md](c4-container.md) — C4 Level 2 container view
- [Controller](../server/controller.md), [node](../server/node.md),
  [operator](../server/operator.md) — per-binary guides
