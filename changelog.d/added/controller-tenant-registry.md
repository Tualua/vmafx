- **vmafx-controller reads and enforces its tenant configuration
  ([ADR-1519](docs/adr/1519-controller-tenant-registry.md)).** With
  `VMAFX_AUTH_TENANTS_SOURCE=kubernetes` the controller lists the
  `VmafxTenant` resources of its namespace (`=file`: a YAML/JSON file of
  them) and accepts only those tenants: each token is verified with the
  identity provider of the tenant it names and must carry that tenant's ID,
  `enabled: false` suspends a tenant (403 / `PERMISSION_DENIED`), roles
  outside `allowedRoles` are dropped and a token without vmafx roles gets
  `defaultRole`. The set is re-read every `VMAFX_AUTH_TENANTS_REFRESH`
  (default 30 s); the controller does not start on an invalid tenant or an
  inconsistent setting, and refuses every token once it has not read its
  tenants for ten intervals. The Helm chart switches to it when
  `auth.tenants` is set (or `auth.tenantSource: kubernetes`), grants the read
  access and opens the API server in the NetworkPolicy.
