---
paths:
  - .github/workflows/libvmaf-build-matrix.yml
invariant: Triple condition on tmate step (failure, macOS, workflow_dispatch) stays intact; step placed after Run tests.
---
# macOS tmate SSH debug step (ADR-0626)

`libvmaf-build-matrix.yml` carries SSH debug step after `Run tests`
step in `libvmaf-build` job:

```yaml
- name: SSH debug session on test failure
  if: ${{ failure() && runner.os == 'macOS' && github.event_name == 'workflow_dispatch' }}
  uses: mxschmitt/action-tmate@c0afd6f790e3a5564914980036ebf83216678101  # v3
```

Rebase-sensitive invariants:

- `if:` triple condition load-bearing. **All three clauses preserved
  together.** Dropping `github.event_name == 'workflow_dispatch'` causes step
  to open blocking SSH session on every failing PR push. Strands macOS runner
  up to 30 minutes per failure.
- Step stays **after** `Run tests` step -> fires only when test failure already
  set job status to `failure()`.
- Action pinned to commit SHA per fork's Renovate
  `helpers:pinGitHubActionDigests` policy. Renovate will propose digest bumps;
  accept only after verifying new SHA corresponds to signed release tag.
- Step is intentionally present in shared matrix job (not separate
  macOS-only job) because `runner.os == 'macOS'` clause in `if:`
  already restricts it to macOS legs. Never split it into separate job.

See [ADR-0626](../../docs/adr/0626-macos-ci-tmate-debug-on-failure.md) and
[`docs/development/ci-tmate-debug.md`](../../docs/development/ci-tmate-debug.md).
