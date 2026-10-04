- **vmafx-operator authenticates to the controller
  ([ADR-1569](docs/adr/1569-operator-controller-auth.md)).** The `VmafxJob`
  reconciler polled `GetJob` without a token, so with the controller's auth
  on every poll failed and no `VmafxJob` left `Pending`. It now reads the
  node's variables: `VMAFX_CONTROLLER_TOKEN_FILE` (read again on every poll,
  so a renewed token applies at once) or `VMAFX_CONTROLLER_TOKEN`, and
  `VMAFX_CONTROLLER_TLS` / `_CA_FILE` / `_SERVER_NAME`. Both programs refuse to
  send a JWT whose `exp` has passed and say which file holds it; a malformed
  combination stops the operator at startup.
