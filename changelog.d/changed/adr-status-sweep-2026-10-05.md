- 78 ADRs that still read `Proposed` although the decision is in force now carry
  the status the tree supports: 77 `Accepted` (one scoped to its Phase 1) and one
  `Superseded`. Fifteen stay `Proposed` with the missing part named in the pull
  request. `scripts/ci/check-adr-status-drift.py` fails when a `Proposed` ADR is
  cited by an old implementing commit, unless an unexpired entry in
  `scripts/ci/adr-status-exceptions.json` names the missing part (5 entries today).
