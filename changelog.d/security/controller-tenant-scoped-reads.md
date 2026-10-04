- **vmafx-controller scopes every job read and every node session to the
  caller's tenant
  ([ADR-1522](docs/adr/1522-controller-tenant-scoped-reads.md)).**
  `StreamJobs` (and the `list_jobs` MCP tool on top of it) returned every
  tenant's jobs; it now returns only the token's tenant's. A node session
  belongs to the tenant whose token registered it: `PullWork` gives the node
  only that tenant's jobs, and the session is refused with another tenant's
  token. `ReportResult` accepts a result only for a job assigned to the
  reporting node, or for a running job of the same tenant whose node is gone
  (a node that registered again after a controller restart); before, any
  node could complete any job, a pending one included. Cross-tenant refusals no longer name the owning tenant. A
  deployment serving several tenants needs a node registration per tenant.
