- **vmafx-controller: compute nodes use a dedicated `vmafx:node` role, and
  `vmafx:admin` no longer reaches the node API
  ([ADR-1563](docs/adr/1563-controller-node-role.md)).** `RegisterNode`,
  `Heartbeat`, `PullWork` and `ReportResult` need `vmafx:node`, which reaches
  no other call; a node's token can no longer read, submit or cancel jobs or
  score, and an administrator token can no longer register a node. The
  `VmafxTenant` CRD offers `vmafx:node` in `rbac.allowedRoles` (not as
  `defaultRole`). **Migration:** issue node tokens with `vmafx:node` instead
  of `vmafx:admin`, and add `vmafx:node` to `allowedRoles` of every tenant
  that runs nodes; until then nodes are refused with
  `role required: vmafx:node`. `VMAFX_AUTH_DISABLED=true` holds both roles.
