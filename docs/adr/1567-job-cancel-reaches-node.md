<!-- markdownlint-disable MD013 MD060 -->
# ADR-1567: a cancelled running job reaches its node through the heartbeat answer, and the node stops the vmaf process

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: go, controller, node, grpc, phase4b, fork-local

## Context

`CancelJob` marked a pending or running job `CANCELLED` in the controller's
queue and nothing else. The node that ran the job never heard of it: its
`vmaf` process ran to the end, held a slot and a GPU, and the node then
reported a result the controller discarded (the job was already terminal).
For long 4K runs that is minutes of wasted device time per cancel, and the
controller's own docs promised "cancellation" without saying it stopped
nothing. The node already talks to the controller every 10 s (`Heartbeat`),
and its executor already runs `vmaf` under `exec.CommandContext`, which kills
the process when the job's context ends.

## Decision

- `HeartbeatRequest` gains `running_job_ids` (the jobs the node runs now, at
  most 64; the controller refuses a longer list) and `HeartbeatResponse`
  gains `cancel_job_ids`. Both fields are additive.
- An accepted heartbeat answers with the entries of `running_job_ids` that
  are `CANCELLED` jobs of the caller's tenant (`queue.CancelledAmong`, tenant
  and status in the SQL `WHERE`). Unknown IDs and other tenants' jobs are left
  out; a refused session names nothing.
- The node runs each job under its own `context.WithCancelCause` and keeps
  the cancel functions by job ID. A named job is cancelled with
  `errCancelledByController`; the `vmaf` process is killed, and the job is
  reported as failed, `cancelled by the controller: ...`. The controller's
  terminal-state guard keeps the job `CANCELLED`.
- The delay is one heartbeat interval (10 s by default,
  `VMAFX_CONTROLLER_HEARTBEAT_INTERVAL`).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Heartbeat answer names cancelled running jobs (chosen) | Uses the call the node already makes; stateless on the controller (the node says what it runs); no new RPC for the role table | Up to one heartbeat interval of delay | Delay is bounded and configurable |
| Server stream from the controller to each node | Immediate | A long-lived stream per node, reconnect logic, a new RPC and role entry | More moving parts than a 10 s bound needs |
| Controller remembers cancels per node and drains them on heartbeat | Node sends nothing extra | State to persist and expire; lost on restart; sends cancels for jobs the node may not run | The node's own list is the truth about what runs |
| Refuse `ReportResult` for cancelled jobs so the node notices | No proto change | The node notices only after the run, which is the waste being fixed | Does not stop the process |

## Consequences

- **Positive**: a cancel stops the scoring process within one heartbeat and
  frees the slot and the device.
- **Negative**: every heartbeat carries up to 64 IDs and costs one indexed
  SQL lookup when it names any.
- **Neutral / follow-ups**: an older node that sends no IDs is never told; it
  behaves as before. `docs/server/controller.md`, `docs/server/node.md`, the
  MCP `cancel_job` entry and the job-lifecycle figure describe the stop.

## References

- [ADR-0711](0711-vmafx-controller-impl.md), [ADR-1522](1522-controller-tenant-scoped-reads.md),
  [ADR-1524](1524-vmafx-node-controller-client.md).
- Follow-up list of 2026-10-04, Lane PLAT item 6: "a cancel on the controller reaches the node running the job (and the node stops the vmaf process)".
