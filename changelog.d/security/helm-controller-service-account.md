- **Only the controller can read `VmafxTenant`s
  ([ADR-1592](docs/adr/1592-helm-split-service-accounts.md)).** With a tenant
  registry the chart bound the tenant reader Role to the service account the
  server, job and node pods share, and the operator's ClusterRole granted
  cluster-wide `vmafxtenants` access for a reconciler that does not exist. The
  controller now runs under its own account, `<name>-controller`, the only one
  bound to the Role, and the operator's tenant rules are gone.
