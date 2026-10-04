<!-- markdownlint-disable MD013 MD060 -->
# vmafx-controller gRPC service

`vmafx-controller` is the distributed platform controller for VMAFX Phase 4b.
It is a single Go binary that exposes VMAF scoring and job orchestration over
both gRPC (default `:9090`) and HTTP/JSON (default `:8080`). Configuration is
by `VMAFX_*` environment variables only.

This page covers the gRPC interface. The HTTP routes are `GET /healthz`,
`GET /readyz`, `GET /metrics` and `POST /v1/score`
(`cmd/vmafx-controller/main.go`); their request and response shapes match the
[vmafx-server REST adapter](rest.md). Authentication is in [auth.md](auth.md).

The controller exposes **two gRPC services** on the same port:

| Service | Purpose |
| --- | --- |
| `VmafxScoring` | Direct scoring (retained from Phase 4a, ADR-0703) |
| `VmafxController` | Job queue + node API (Phase 4b.1, ADR-0711) |

## Quick start

1. Run the controller locally. It needs a `vmaf` binary, for example from
   `core/build-cpu`.

    ```bash
    VMAFX_VMAF_BINARY=core/build-cpu/tools/vmaf \
    VMAFX_MODEL_DIR=model/ \
    VMAFX_HTTP_ADDR=:8080 \
    VMAFX_GRPC_LISTEN=:9090 \
    VMAFX_DB_PATH=/tmp/vmafx-controller.db \
    go run ./cmd/vmafx-controller
    ```

2. Or build and run the container image.

    ```bash
    docker build -f docker/Dockerfile.controller -t vmafx-controller:dev .
    docker run --rm \
        -e VMAFX_VMAF_BINARY=/usr/local/bin/vmaf \
        -e VMAFX_MODEL_DIR=/usr/local/share/vmafx/model \
        -e VMAFX_DB_PATH=/data/vmafx-controller.db \
        -p 8080:8080 -p 9090:9090 \
        vmafx-controller:dev
    ```

## Configuration

All settings are environment variables (12-factor); the controller has no CLI
flags beyond `--version` since ADR-1119. Listen addresses are full addresses
(`:8080`), not bare ports.

| Env var | Default | Description |
| --- | --- | --- |
| `VMAFX_HTTP_ADDR` | `:8080` | HTTP listen address |
| `VMAFX_GRPC_LISTEN` | `:9090` | gRPC listen address |
| `VMAFX_LOG_LEVEL` | `INFO` | slog level (DEBUG/INFO/WARN/ERROR) |
| `VMAFX_VMAF_BINARY` | _(PATH lookup)_ | Path to the `vmaf` CLI binary |
| `VMAFX_MODEL_DIR` | _(none)_ | Directory containing VMAF `.json` model files |
| `VMAFX_DB_PATH` | `vmafx-controller.db` | Path to the SQLite job-persistence database |

The authentication variables (`VMAFX_AUTH_DISABLED`, `VMAFX_JWKS_ENDPOINT`,
`VMAFX_AUTH_ISSUER`, `VMAFX_AUTH_AUDIENCE`, `VMAFX_AUTH_TENANT_CLAIM`,
`VMAFX_AUTH_ROLES_CLAIM`) are listed in
[auth.md](auth.md#environment-variables).

## VmafxScoring service (direct scoring)

Retained from Phase 4a for backward compatibility.  Clients that already talk to
`vmafx-server` continue to work against `vmafx-controller` without changes.

```protobuf
service VmafxScoring {
  rpc Score(ScoreRequest)   returns (ScoreResponse);
  rpc ScoreStream(stream ScoreStreamRequest)
      returns (stream ScoreStreamResponse);  // bidirectional, per-frame (ADR-0933)
  rpc Health(HealthRequest) returns (HealthResponse);
}
```

Proto source: `proto/vmafx.proto`.  Generated stubs: `gen/go/vmafxv1/`.

### Example: direct score

```bash
grpcurl -plaintext \
    -d '{"reference":"/data/ref.yuv","distorted":"/data/dis.yuv","model":"vmaf_v0.6.1"}' \
    localhost:9090 vmafx.v1.VmafxScoring/Score
```

## VmafxController service (job queue + node API)

Phase 4b.1 distributed orchestration surface.  Proto source:
`cmd/vmafx-controller/proto/controller.proto`. Generated stubs:
`gen/go/controller/`.

### Client API

Used by the CLI, MCP server, and future Web UI to submit and track jobs. The
full service definition is in `cmd/vmafx-controller/proto/controller.proto`.

| RPC | Caller | Purpose |
| --- | --- | --- |
| `SubmitJob` | client | Enqueue a job for the caller's tenant; returns its ID |
| `GetJob` | client | Read the current state of one of the tenant's jobs |
| `CancelJob` | client | Request cancellation of one of the tenant's pending or running jobs |
| `StreamJobs` | client | Server-streaming snapshot of the tenant's jobs (optional status filter); a snapshot in Phase 4b.1 |
| `RegisterNode` | node | Register a worker with its capability; the session belongs to the caller's tenant |
| `Heartbeat` | node | Keep the registration alive |
| `PullWork` | node | Receive the next matching job of the node's tenant |
| `ReportResult` | node | Report progress or the final result of a job assigned to the node |

Every call carries the tenant of its token, and every job it reads or writes
belongs to that tenant; the role each call needs and the tenant rules are in
[Auth gateway](auth.md#roles-and-rbac).

#### Submit a job

```bash
grpcurl -plaintext \
    -d '{"scoring":{"reference":"/data/ref.yuv","distorted":"/data/dis.yuv","backend":"cuda"}}' \
    localhost:9090 vmafx.controller.v1.VmafxController/SubmitJob
# → {"jobId": "550e8400-e29b-41d4-a716-446655440000"}
```

#### Poll job status

```bash
grpcurl -plaintext \
    -d '{"jobId":"550e8400-e29b-41d4-a716-446655440000"}' \
    localhost:9090 vmafx.controller.v1.VmafxController/GetJob
# → {"id":"...","status":"COMPLETED","scoring":{...},"assignedNode":"node-abc"}
```

### Node API

Used by `vmafx-node` worker processes to pull and report work (the node RPCs in
the table above).

Node lifecycle (`vmafx-node` implements it when `VMAFX_CONTROLLER_ADDR` is
set; see [node.md](node.md#pulling-jobs-from-the-controller)):

1. On startup, the node calls `RegisterNode` with its capability (GPU vendor,
   available backends, concurrency slots).  The controller returns a `node_id`
   and a `session_token`.
2. The node calls `Heartbeat` every ~10 s with the `node_id` and
   `session_token`.
   A node that misses heartbeats for 60 s is evicted, and its running jobs
   return to `PENDING` ahead of newer work, so another node picks them up. A
   node that comes back registers again; if it still finishes such a job, the
   first final result the controller receives is kept.
3. When the node has capacity, it calls `PullWork`.  The controller assigns the
   oldest `PENDING` job whose `backend` requirement matches the node's
   capabilities.
4. After the job completes (or fails), the node calls `ReportResult` with
   `final=true`.

### Job lifecycle

```figure
controller-job-lifecycle
```

### Backend capability matching

A job's `scoring.backend` field specifies which backend the job requires
(e.g. `"cuda"`, `"sycl"`, `"cpu"`).  If empty, any node can accept the job.
A node must list the required backend in its `capability.backends` to receive
the job.

## Job persistence

The controller persists jobs in the SQLite database at `VMAFX_DB_PATH`.
On controller restart:

- `PENDING` jobs are reloaded and re-queued in submission order.
- `RUNNING` jobs are reset to `PENDING` (their assigned nodes are gone).
- `COMPLETED`, `FAILED`, and `CANCELLED` jobs are retained for audit.

Schema: `cmd/vmafx-controller/queue/schema.sql`.

## Prometheus metrics

`/metrics` exposes the following in Prometheus exposition format. The scoring
counters and histogram are the metric set shared with `vmafx-server`
(`pkg/observability/observability.go`), so they carry the `vmafx_server_`
prefix; the queue and node metrics carry `vmafx_controller_`.

| Metric | Type | Description |
| --- | --- | --- |
| `vmafx_server_score_requests_total` | Counter | Direct Score requests (HTTP + gRPC VmafxScoring) |
| `vmafx_server_score_errors_total` | Counter | Direct Score requests that returned an error |
| `vmafx_server_score_duration_seconds` | Histogram | Direct scoring latency |
| `vmafx_server_health_requests_total` | Counter | Health / `/healthz` calls |
| `vmafx_server_ready_requests_total` | Counter | `/readyz` calls |
| `vmafx_controller_jobs_pending` | Gauge | Current number of PENDING jobs |
| `vmafx_controller_jobs_running` | Gauge | Current number of RUNNING jobs |
| `vmafx_controller_nodes_live` | Gauge | Current number of live registered nodes |
| `vmafx_controller_jobs_submitted_total` | Counter | Jobs submitted via SubmitJob RPC |
| `vmafx_controller_jobs_completed_total` | Counter | Jobs completed successfully |
| `vmafx_controller_jobs_failed_total` | Counter | Jobs that ended in failure |

There is no cancelled-jobs counter.

## Graceful shutdown

The controller listens for `SIGTERM` and `SIGINT`. On receipt it:

1. Stops accepting new connections.
2. Waits up to 30 seconds for in-flight requests to drain.
3. Exits with code 0.

Jobs remain in the SQLite database; the next controller instance reloads them.

## Further reading

- [ADR-0711](../adr/0711-vmafx-controller-impl.md) — Phase 4b.1 decision record.
- [ADR-0709](../adr/0709-vmafx-phase4b-distributed-platform.md) — Phase 4b
  umbrella architecture.
- [ADR-0703](../adr/0703-vmafx-server-go-grpc.md) — Phase 4a origin
  (vmafx-server).
- [REST adapter](rest.md) — `/healthz`, `/readyz`, `/metrics`, `/v1/score`
  request and response shapes.
- [Auth gateway](auth.md) — JWT authentication and tenant isolation.
- [k8s deployment guide](../development/k8s-deployment.md) — Helm chart
  configuration.
