- **The Helm chart no longer renders auth settings that nothing applies
  ([ADR-1519](docs/adr/1519-controller-tenant-registry.md)).** `auth.enabled`
  passed `VMAFX_AUTH_*` to the default `vmafx-server` image, which has no auth
  gateway, so the server ran unauthenticated; the render now fails unless
  `image.repository` names a vmafx-controller image and `workload` is
  `Deployment`. `enabled: false` in an `auth.tenants` entry rendered as
  `true`; it now renders as `false`.
