- **Cancelling a running job stops it on its node
  ([ADR-1567](docs/adr/1567-job-cancel-reaches-node.md)).** `CancelJob` used
  to mark the job `CANCELLED` while the node's `vmaf` process ran on to the
  end. The node's heartbeat now lists the jobs it runs, the controller answers
  with the cancelled ones, and the node kills their `vmaf` processes and
  reports them as `cancelled by the controller`, within one heartbeat interval
  (`VMAFX_CONTROLLER_HEARTBEAT_INTERVAL`, 10 s by default). The two new
  heartbeat fields are additive; an older node is not told.
