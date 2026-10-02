---
paths:
  - scripts/ci/upstream_parity_pin.py
  - scripts/ci/tests/test_upstream_parity_pin.py
invariant: One parity heading = upstream pin of required Licence Provenance job; port or sync moves id, wording stays.
---
<!-- markdownlint-disable MD013 MD060 -->
# Licence provenance job and recorded upstream head

## Workflow coupling

| Script | Invoked by | Coupling |
| --- | --- | --- |
| `upstream_parity_pin.py` | `lint-and-format.yml` — `licence-provenance` job, check name `Licence Provenance` ([ADR-1474](../../../docs/adr/1474-relicense-helper-headers-and-ci-check.md)); `test-upstream-parity-pin` pre-commit hook | Reads one heading of `docs/development/known-upstream-bugs.md`: ``## Upstream head the fork is at parity with: `<commit id>` (<date>)``. Job passes resolved id to `scripts/dev/relicense_fork_files.py --check --upstream-ref`. Zero headings, two headings, reworded heading, id outside Netflix `master` -> exit 2, job red. |

## Invariants

- Pin = recorded parity head, never upstream moving `master`: Netflix push never
  turns unrelated pull request red. Upstream port or sync moves id in same
  pull request.
- Job needs full history both sides: `fetch-depth: 0`, unshallow fetch of
  Netflix `master`. `relicense_fork_files.py` refuses shallow checkout
  (`require_full_history()`); keep that guard.
- `Licence Provenance` sits in aggregator `required` + `strictMustReport` and in
  `ADR_1474_STRICT_CONTEXTS` (`tests/test_hiss_replay_contract.py`). Rename in
  all places, same commit. No path filter, no `continue-on-error`.
- `tests/test_upstream_parity_pin.py` pins heading grammar, resolution and job
  text. Change parser, heading form or job steps -> change test, same commit.
- Human guide: `docs/development/licence-provenance-check.md`.
