- The repository no longer carries `.github/rulesets/main.json`, praetor's
  template that declared two approvals, code-owner review and signed commits
  while the live ruleset on `master` enforces one approval and neither of the
  others. `.standards.yaml` declines the template (`adoption.decline:
  [branch-ruleset]`) and `repository-security-policy.json` stays the single
  declaration, compared with the live ruleset. A new test pins the decline, the
  absence and the declared review values. The live ruleset is unchanged.
