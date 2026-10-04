<!-- markdownlint-disable MD013 MD060 -->
# AGENTS.md — cmd/vmafx-controller

Go controller service, VMAFX distributed platform (ADR-0711, ADR-0709).
Exposes gRPC `VmafxController` (queue + node API) and `VmafxScoring`
(direct scoring) on single port, plus HTTP `/healthz /readyz /metrics /v1/score`.

Per-package invariants for subtree.

## Governing ADRs

| ADR | Title | Scope |
|-----|-------|-------|
| [ADR-0711](../../docs/adr/0711-vmafx-controller-impl.md) | vmafx-controller Phase 4b.1 | Go service: gRPC + HTTP, queue, node registry, FIFO scheduler |
| [ADR-0961](../../docs/adr/0961-queue-pullwork-rollback-on-get-failure.md) | PullWork rollback on post-update Get failure | queue package correctness |
| [ADR-0962](../../docs/adr/0962-controller-streamjobs-and-reaper-stop.md) | StreamJobs snapshot + reaper stop signal | controller / queue / nodes correctness |
| [ADR-1119](../../docs/adr/1119-golusoris-go-framework-adoption.md) | golusoris fx framework adoption | composition root, env contract, lifecycle ordering, auth injection |
| [ADR-1518](../../docs/adr/1518-controller-grpc-authorization.md) | gRPC authorisation: per-method role table, deny by default | auth interceptors, `grpc_roles.go` |

## Protobuf bindings (ADR-1119) — GENERATED, never hand-written

`controllerv1` Go bindings at `gen/go/controller/{controller,controller_grpc}.pb.go`
generated from `cmd/vmafx-controller/proto/controller.proto` via
`cmd/vmafx-controller/proto/generate.sh` (`//go:generate ./generate.sh`).

- **NEVER hand-edit `.pb.go` files.** Hand-written stubs lacked `proto.Message`
  implementation (no `protoimpl`/`ProtoReflect`); `VmafxController` RPCs
  failed marshaling while unit tests passed. Guard:
  `cmd/vmafx-controller/wire_test.go` round-trips `SubmitJob`/`GetJob`/`StreamJobs`
  over `bufconn` `grpc.Server`. Edit `controller.proto`, regenerate, keep test green.
- Job-status enum: **top-level `JobStatus`** (not nested in `Job`); Go type:
  `controllerv1.JobStatus`, matching grpc_server/queue/scheduler. Nesting in
  `Job` generates `Job_Status`, breaking call sites.

## fx composition (ADR-1119)

Controller wired via `fx.New(...).Run()` over golusoris framework.
`main.go` supplies vmafx providers + invokes; golusoris owns config (koanf,
`VMAFX_` prefix, `.` delimiter), structured slog logging, OTel, HTTP stack
(`golusoris.HTTP` with chi router + graceful `*http.Server`), gRPC server
(OTel + logging + panic-recovery interceptors), shutdown.

1. **`productionOptions(envReplace)` = single graph source.** `main` and
   `app_test.go` build from it. `fx.Replace` parameterised (fx forbids duplicate
   type replacement): binary passes `Watch:true`, tests `Watch:false`.
2. **SQLite job queue KEPT — do NOT adopt `golusoris.Jobs`.** `provideJobQueue`
   wraps `modernc.org/sqlite` queue with `OnStop` `Close`. Single-binary
   queue: ADR-1119 decision; river/Postgres out of scope.
3. **JWT auth injected via golusoris#269 (`ProvideServerOptionFn`).** Interceptors
   wired via `grpc.ProvideServerOptionFn(func(mw *auth.Middleware) grpc.ServerOption{...})`
   into `group:"grpc.serveropts"`. fx injects `*auth.Middleware`. Do NOT reintroduce
   package holder / per-RPC lookup (`globalAuthMW` `#225`). v0.6.0 native way.
   Do not construct Middleware in composition root.
4. **Lazy-provider bind guards load-bearing.** `fx.Invoke(func(_ *http.Server){})`
   and `fx.Invoke(func(_ *grpc.Server){})` force binding HTTP and gRPC listeners.
   Tested in `TestAppStartsAndStops` and `TestGRPCListenerBindsAndServes`.
5. **Stop order (R1).** `fx.Invoke(func(_ *libvmaf.Scorer, _ queue.Queue, _ *nodes.Registry){})`
   registered AHEAD of gRPC registration; scorer/queue/registry OnStop hooks
   run in reverse: gRPC `GracefulStop` → queue `Close` + reaper stop → scorer
   `Close`. Guard: `TestStopOrder`.
6. **gen/go/controller proto hand-written.** `gen/go/controller/*.pb.go`
   lacks protobuf-v2 reflection interface; `VmafxController` RPCs unmarshaled
   by standard codec. Wire tests use `VmafxScoring`; in-process handler
   tests call controller methods directly.

## Invariants

### queue package

1. **PullWork rollback completeness (ADR-0961)**: 3-step rollback in
   `PullWork` (SQL UPDATE to `pending`, `runningSet` delete, FIFO prepend)
   atomic under `q.mu`. Do not early-return between
   `q.runningSet[matchID] = struct{}{}` and `getUnlocked` without updating
   `rollbackTopending`.
2. **`getUnlockedHook` test-only (ADR-0961)**: `getUnlockedHook` and
   `SetGetUnlockedHookForTest` prohibited in production. `ForTest` suffix
   = naming contract.
3. **runningSet / pendingFIFO consistent**: SQL job status changes mirror in
   `runningSet` and `pendingFIFO`. `reload()`: recovery on restart, not
   primary mechanism.
4. **`Queue.ListAll` contract (ADR-0962)** (`queue/queue.go`): `ListAll(ctx, statuses)`
   returns snapshot of jobs filtered by status strings. Empty `statuses` =
   all statuses. `StreamJobs` in `grpc_server.go` relies on contract.
5. **`ListAll` must include `tenant_id` in SELECT** (`queue/queue.go`):
   queries select `COALESCE(tenant_id,'')` into `job.TenantID` (`Job.TenantID` was `""`).
   Missing `tenant_id` broke `StreamJobs` tenant display. Schema additions must
   update both SELECT clauses and `rows.Scan`. Guard:
   `queue_listall_test.go:TestListAll_TenantIDRoundTrip`.

### scheduler package

- Invariants pending scheduler ADR.

### nodes package

1. **`nodes.NewRegistry` Start/Close lifecycle (ADR-1119)** (`nodes/registry.go`):
   `NewRegistry(log *slog.Logger)` takes no context, spawns no reaper. Reaper
   started by `Start(ctx)` (fx `OnStart` in `provideNodeRegistry`), stopped by
   `Close()` (fx `OnStop`). Reaper bound to `Close`-owned context. Tests call
   `Close()` in `t.Cleanup`. `Close()` safe without `Start()`. Replaced
   eager `NewRegistry(ctx)` (ADR-0962).

### grpc server

1. **`protoStatusToQueue` / `queueStatusToProto` sync (ADR-0962)** (`grpc_server.go`):
   inverse helpers. New `Job.Status` requires updating both plus `queue.Status*`.
2. **`grpc_server_test.go` mock stream (ADR-0962)**: `mockStreamJobsServer`
   implements `grpc.ServerStream` (6 methods inline).
3. **Per-RPC role policy (ADR-1518)** (`grpc_roles.go`, `auth/policy.go`):
   `controllerMethodRoles()` names roles for every served method; auth
   interceptors authenticate then authorise in one function (`admitGRPC`).
   Unlisted method = refused for every caller, disabled-mode admin included.
   New RPC -> add entry in same PR; `TestEveryServedRPCHasARolePolicy` fails
   otherwise. Never chain separate role interceptor; never move role checks
   into handlers. `TestGRPCRolesEnforcedPerRPC` holds independent
   expectation table: change only together with ADR-0794/ADR-1518 role table.
4. **`auth/authtest` test-only**: RS256 issuer + JWKS server for tests.
   Import from `_test.go` files only.

### main / shutdown

1. **Shutdown ordering (ADR-1119)** (`main.go`): graceful shutdown owned by
   fx lifecycle. Order: gRPC `GracefulStop` → queue `Close` + node reaper stop
   → scorer `Close`. Replaced `observability.NewShutdownContext()` / `errgroup` /
   `runHTTP`/`runGRPC`. Guard: `TestStopOrder`.

### observability

1. **OTel from `bootstrap.Base` + `bootstrap.HTTPTracing`, spans from golusoris and `grpc_server.go`**
   (`main.go::productionOptions`, ADR-0782 / ADR-1119): `bootstrap.HTTPTracing`
   traces `POST /v1/score` with `otelhttp`; gRPC server spans via `grpcmod.Module`
   `otelgrpc`; child span `vmafx.job.submit` via `observability.StartSpan`.
   Guards: `app_test.go::TestOTelWiredThroughBootstrap` locks no-op default,
   `vmafx-controller` / `pkg/version`; `TestGRPCHealthEmitsLinkedSpans` proves
   propagation (client/server share trace).
