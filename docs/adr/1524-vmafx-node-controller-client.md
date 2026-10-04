<!-- markdownlint-disable MD013 MD060 -->
# ADR-1524: vmafx-node pulls work from the controller, advertises exactly the backend it runs, and refuses to start when it cannot honour its configuration

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: go, node, controller, grpc, phase4b, security, fork-local

## Context

[ADR-0713](0713-vmafx-node-impl.md) describes the node loop: register with the
controller, heartbeat, pull work, execute, report. The controller has served
the Node API since [ADR-0711](0711-vmafx-controller-impl.md), but
`cmd/vmafx-node` contained no caller of it, and the Helm chart set a
`VMAFX_CONTROLLER_ADDR` that no code read (docs audit of 2026-10-03, defect
33). Jobs submitted to the controller therefore stayed `PENDING` for ever. The
maintainer decided (popup of 2026-10-04) that the unwired platform features
are implemented, not stubbed, and that no requested option or path is
silently replaced.

Implementing the loop raises choices another engineer could make differently:
what a node advertises, what it does when it cannot score, how it
authenticates, how it retries and what happens to a running job at shutdown.

## Decision

- The node runs a controller client when `VMAFX_CONTROLLER_ADDR` is set and
  logs that it runs standalone when it is not. One session keeper registers
  (unbounded attempts, jittered exponential backoff from 0.5 s to 30 s) and
  heartbeats every `VMAFX_CONTROLLER_HEARTBEAT_INTERVAL`; `VMAFX_NODE_SLOTS`
  pull loops call `PullWork`, run the job through the `Executor` and report a
  final `ReportResult` (up to 8 attempts). Every RPC has its own deadline
  (`VMAFX_CONTROLLER_RPC_TIMEOUT`).
- A heartbeat answered `ok=false`, a call refused with `PermissionDenied`, or
  60 s without an accepted heartbeat (the controller's eviction window) drops
  the session; the node registers again and reports a job it finished under
  the new session.
- The node advertises one backend, the `VMAFX_BACKEND` it runs (`cpu`,
  `cuda`, `hip`, `sycl` or `metal`), and the executor passes the job's backend
  (or the node's) to the vmaf CLI as `--backend`, so a job runs on the backend
  it was scheduled for or fails. `auto` cannot be advertised and is refused.
- The node refuses to start when the client is enabled but no vmaf scorer
  exists, and when any controller setting is malformed (duration, slot count
  outside 1 to 64, unreadable CA, token given twice, CA without TLS).
- Authentication is the controller's: a bearer token from
  `VMAFX_CONTROLLER_TOKEN_FILE` (re-read on every call, so a rotated token
  applies) or `VMAFX_CONTROLLER_TOKEN`; TLS when `VMAFX_CONTROLLER_TLS=true`,
  in which case gRPC never sends the token in plaintext.
- At shutdown the client stops pulling, lets a running job finish until the
  stop deadline, then cancels it and reports it as failed ("node shutting
  down"), so the controller does not keep a job `RUNNING` for a node that is
  gone. A non-finite score or feature is reported as a failure naming it.
- The Helm chart renders `VMAFX_CONTROLLER_ADDR` only from
  `node.controllerAddr` (it deploys no controller, so it has no default) and a
  NetworkPolicy egress rule from the nodes to the controller's gRPC port.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Advertise every backend `pkg/gpu.Detect()` finds | No configuration | The vmaf CLI picks its own backend; a node advertising CUDA would score a CUDA job wherever the CLI chose, a silent substitution | The node advertises what it runs and passes `--backend` |
| Pull work with a missing scorer and fail every job | Node still starts | Every job the scheduler hands it fails; the queue drains into failures | A node that cannot score must not pull |
| Leave a running job unreported at shutdown | Simpler stop | The controller's reaper drops the node but not its jobs; the job stays `RUNNING` | Report it failed with the reason |
| Default `VMAFX_CONTROLLER_ADDR` to `<release>-controller:8080` (previous chart) | Works out of the box when such a Service exists | The chart deploys no such Service and 8080 is the controller's HTTP port; every node would retry for ever | No default; empty runs standalone |
| Token only from an environment variable (vmafx-mcp's way) | One source | A projected service-account token rotates; an env var is fixed at start | File re-read per call, inline token kept for parity |

## Consequences

- **Positive**: a job submitted to the controller is pulled, scored and
  reported; `TestEndToEndControllerNodeJob` runs a real controller, the
  node's production graph and the real vmaf CLI.
- **Positive**: misconfiguration stops the node at startup with a message
  naming the setting instead of a node that never works.
- **Negative**: a node serves one backend; a host with two GPUs of different
  vendors runs two node processes.
- **Neutral / follow-ups**: the controller's reaper evicts a node but leaves
  its running jobs `RUNNING` (tracked in `docs/state.md`); a job's
  cancellation does not reach the node, which finishes the job and whose
  report the controller then ignores.

## References

- [ADR-0713](0713-vmafx-node-impl.md), [ADR-0711](0711-vmafx-controller-impl.md),
  [ADR-0794](0794-controller-multi-tenant-auth-gateway.md),
  [ADR-1119](1119-golusoris-go-framework-adoption.md).
- Docs audit defect list of 2026-10-03, defect 33.
- Popup answer of 2026-10-04, "Unwired distributed-platform features": "Implement them now".
- Popup answer of 2026-10-04, "Defects": "Track all, fix now (Recommended)".
