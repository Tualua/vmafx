- **Each tenant scores only inputs under its own scoring roots
  ([ADR-1577](docs/adr/1577-scoring-paths-per-tenant.md)).** `Score`,
  `POST /v1/score` and `SubmitJob` took any path, URL or rclone remote, so on
  shared storage one tenant's writer could score another tenant's media or
  any file the controller or a node can open. An input must now lie under one
  of the caller tenant's roots (`VmafxTenant.spec.scoring.roots`, or
  `VMAFX_SCORING_ROOTS` with `{tenant}` without a tenant registry; Helm
  `auth.tenants[].scoring.roots` / `auth.scoringRoots`). `..` is refused and
  symlinks are resolved where the files are read: on the controller for
  direct scoring, on the node for jobs (the controller sends the roots with
  each job). **Migration:** a controller without roots refuses every input
  (deny by default); configure the roots before upgrading.
