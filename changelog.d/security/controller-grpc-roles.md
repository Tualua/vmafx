- **vmafx-controller enforces roles on every gRPC call
  ([ADR-1518](docs/adr/1518-controller-grpc-authorization.md)).** The roles of
  the auth gateway gated only HTTP `POST /v1/score`; any valid token could call
  every gRPC method, including `SubmitJob`, `CancelJob` and the node API. Each
  call now needs a role from the token: `vmafx:reader` for `GetJob`,
  `StreamJobs` and `Health`, `vmafx:writer` for `SubmitJob`, `CancelJob`,
  `Score` and `ScoreStream`, `vmafx:admin` for `RegisterNode`, `Heartbeat`,
  `PullWork` and `ReportResult`; other tokens get `PERMISSION_DENIED`
  (`role required: ...`). A method missing from the role table is refused for
  every caller. Clients with auth enabled need tokens carrying these roles
  (`VMAFX_CONTROLLER_TOKEN` of `vmafx-mcp`: writer to submit and cancel).
