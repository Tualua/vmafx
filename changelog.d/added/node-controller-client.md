- **`vmafx-node` pulls jobs from the controller.** With
  `VMAFX_CONTROLLER_ADDR` set, the node registers with the controller,
  heartbeats, takes jobs with `PullWork`, scores them on the backend it
  advertises (`VMAFX_BACKEND`, passed to the vmaf CLI as `--backend`) and
  reports the results; before, no component called the controller's Node API
  and submitted jobs stayed `PENDING`. Retries use jittered backoff, every call
  has a deadline, a refused session is renewed, a bearer token comes from
  `VMAFX_CONTROLLER_TOKEN_FILE` (re-read per call) or `VMAFX_CONTROLLER_TOKEN`,
  and TLS is `VMAFX_CONTROLLER_TLS`. The node refuses to start without a vmaf
  binary, with `VMAFX_BACKEND=auto` or with a malformed setting. The Helm chart
  sets the address only from `node.controllerAddr` (no default pointing at a
  missing Service) and opens node-to-controller egress under
  `networkPolicy.enabled`. See [the node guide](docs/server/node.md#pulling-jobs-from-the-controller)
  and [ADR-1524](docs/adr/1524-vmafx-node-controller-client.md).
