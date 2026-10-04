- **vmafx-controller refetches its JWKS keys and hides why a token was refused
  ([ADR-1519](docs/adr/1519-controller-tenant-registry.md)).** A key the
  identity provider withdrew kept verifying tokens until the controller
  restarted; keys are now refetched after 15 minutes (and kept for at most
  24 hours while the endpoint fails), failed fetches are rate-limited like
  successful ones, and an `https` JWKS fetch no longer follows a redirect to
  plain `http`. A refused gRPC call now gets the fixed message `invalid or
  missing token` instead of the verification error, which named the
  configured issuers.
