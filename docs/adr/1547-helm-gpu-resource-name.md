<!-- markdownlint-disable MD013 MD060 -->
# ADR-1547: The Helm chart derives the GPU resource name from the vendor and the Intel kernel driver, with an explicit override

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: helm, k8s, gpu, intel, fork-local

## Context

The chart requested `gpu.intel.com/i915` for every Intel GPU, written out in
two helpers of `deploy/helm/vmafx/templates/_helpers.tpl` (docs audit of
2026-10-03, defect 36). The Intel device plugin names the resource after the
GPU's kernel driver: `gpu.intel.com/xe` for GPUs on `xe` (Arc B-series and
newer, or an older GPU bound to `xe`). On such a node a VMAFX pod stayed
`Pending`. The maintainer's home cluster advertises `gpu.intel.com/xe` on its
Arc B580 and Pro B60 nodes and `gpu.intel.com/i915` on its Arc A380 node.

## Decision

- `gpu.intelDriver` (`i915` | `xe`, default `i915`) selects the Intel
  resource: `gpu.intel.com/<intelDriver>`.
- `gpu.resourceName` requests any extended resource verbatim (validated as
  `domain/name`), overriding the vendor default and `intelDriver`; it is
  refused with `gpu.vendor: cpu`. The backend still follows `gpu.vendor`.
- One helper, `vmafx.gpuResourceName`, resolves the name for every workload;
  `vmafx.gpuResource` adds the `gpu.enabled` gate; the duplicate
  `vmafx.gpuResourceKey` is removed.
- The default stays `i915`, so a values file that rendered before renders the
  same manifest.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Default `intelDriver` to `xe` | Matches the newest GPUs | Changes the request of every existing Intel deployment without notice | Default kept; `xe` documented |
| Request both resources | No choice for the operator | A pod requests what one node never has both of; it would never schedule | Not chosen |
| Only `gpu.resourceName` | One value | The common Intel case needs the plugin's exact name typed by hand, unvalidated | Driver enum for the common case, override for the rest |
| Detect the driver at install time | No configuration | Helm renders without cluster access in CI and GitOps; `lookup` returns nothing there | Not chosen |

## Consequences

- **Positive**: Intel GPUs on `xe` schedule with `gpu.intelDriver: xe`;
  sharing and MIG resources work through `gpu.resourceName`.
- **Negative**: one more value to set for `xe` nodes.
- **Neutral / follow-ups**: `scripts/ci/tests/test_helm_node_contract.py`
  renders the server and node Deployments for every vendor, `xe`, an override,
  and the refusals.

## References

- Docs audit defect list of 2026-10-03, defect 36.
- The maintainer's home-cluster configuration (a private repository, read
  only): its Intel device plugin advertises `gpu.intel.com/xe` for the B580
  and B60 nodes.
- Popup answer of 2026-10-04, "Defects": "Track all, fix now (Recommended)".
