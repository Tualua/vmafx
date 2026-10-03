<!-- markdownlint-disable MD013 MD060 -->
# ADR-1504: Decline praetor's branch ruleset; the policy file is the only declaration

- **Status**: Accepted
- **Date**: 2026-10-03
- **Deciders**: Lusoris
- **Tags**: security, ci, governance, praetor, fork-local

## Context

Since the praetor adoption ([ADR-1249](1249-praetor-governance-adoption.md)) the
repository carried `.github/rulesets/main.json`, praetor's rendering of a branch
ruleset: two approving reviews, code-owner review, signed commits and every
check praetor derives from the workflows. It was never the live declaration.
The live ruleset `VMAFx master security` (id 22587111,
[ADR-1252](1252-solo-maintainer-declared-bypass.md)) enforces one approval, no
code-owner review, no required signatures and the single check `Required Checks
Aggregator`, and `.github/repository-security-policy.json` declares exactly
that, verified against the live ruleset by `scripts/dev/check_repository_security.py`.
[ADR-1249](1249-praetor-governance-adoption.md) and
[ADR-1351](1351-praetor-engine-pin-move.md) both called the template "a local
declaration" and "praetor's template", but a reader of the repository still saw
two approvals and code-owner review next to a ruleset that asks for neither.

praetor's audit accepts that file only when it equals praetor's own rendering,
and the renderer cannot express one approval without code-owner review: the
single-maintainer mode renders zero approvals, the independent mode renders the
configured count with code-owner review forced on. The file therefore cannot be
edited to state what is enforced.

## Decision

Decline the template. `.standards.yaml` carries
`adoption.decline: [branch-ruleset]`, `.github/rulesets/main.json` is removed,
and `.github/repository-security-policy.json` is the single declaration of the
ruleset, checked against the live one. A test in
`scripts/dev/tests/test_repository_security.py` fails when the template returns,
the decline goes, or a declared review value differs from the live one. The live
ruleset is unchanged.

This replaces the statements of ADR-1249 (follow-up "the generated
`.github/rulesets/main.json` is therefore a local declaration only") and
ADR-1351 (consequence "the rendered `.github/rulesets/main.json` ... asks for
two approvals and signed commits") that a rendered template is kept in the
repository. The rest of both ADRs stands.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Edit `main.json` to equal the live ruleset | One file, no decline | praetor's audit rejects any file that is not its rendering (`ErrRulesetDrift`); the commit hook refused it | Cannot pass the gate |
| Approximate live through `.standards.yaml` `branch_protection` and keep the rendering | The file stays praetor-managed | Still claims code-owner review and signed commits, and 28 contexts the live ruleset does not require; the repository looks stricter than it is | ADR-1252 forbids that appearance |
| Keep the template as is | No change | Declares two approvals, code-owner review and signatures that are not enforced | Same appearance problem |
| Decline the template and remove the file (chosen) | One declaration, already drift-checked; nothing false in the tree | Gives up praetor's rendering of the ruleset | |

## Consequences

- **Positive**: no file in the tree claims protection the repository lacks.
- **Negative**: a praetor change that makes the renderer express the live
  values would need a new decision to adopt it again.
- **Neutral / follow-ups**: a pin move or `sync` must not bring the template back;
  the test fails if it does. See [repository security](../development/repository-security.md).

## References

- Q (popup, 2026-10-03): "Declared file matches live (Recommended)". The file is
  removed rather than edited because praetor's audit accepts only its own
  rendering of that path, and that rendering cannot state the live values.
- [ADR-1249](1249-praetor-governance-adoption.md), [ADR-1252](1252-solo-maintainer-declared-bypass.md),
  [ADR-1351](1351-praetor-engine-pin-move.md).
