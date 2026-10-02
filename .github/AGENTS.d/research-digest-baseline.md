---
paths:
  - .github/workflows/rule-enforcement.yml
  - scripts/ci/check-research-digest-ids.py
invariant: Rules workflow passes PR exact base.sha to check-research-digest-ids; full-history checkout; no bootstrap-from-ref.
---
# Research-digest baseline authority (ADR-1335)

blocking Rules workflow passes pull request's exact `base.sha` to
`check-research-digest-ids.py`. Preserve that binding and full-history
checkout: checker resolves merge base and rejects branch baseline
that grows collision or H1 debt beyond trusted authority. CI and hooks must
never invoke `--bootstrap-from-ref`; that full-commit, pre-ratchet path is
bounded one-time operator action only.
