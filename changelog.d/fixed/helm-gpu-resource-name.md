- **Helm: Intel GPUs on the `xe` kernel driver schedule.** The chart requested
  `gpu.intel.com/i915` for every Intel GPU, so pods on nodes whose GPUs run on
  `xe` (Arc B-series and newer) stayed `Pending`. `gpu.intelDriver: xe`
  requests `gpu.intel.com/xe` (the default stays `i915`), and
  `gpu.resourceName` requests any other resource verbatim, such as a sharing
  or MIG resource. The NetworkPolicy rule for controller-to-node traffic now
  opens the node's gRPC port (`node.grpcPort`, 50052) instead of a fixed 50051.
  See the [GPU scheduling guide](docs/development/gpu-scheduling.md) and
  [ADR-1547](docs/adr/1547-helm-gpu-resource-name.md).
