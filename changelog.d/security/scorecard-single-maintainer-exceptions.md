- **The single-maintainer gaps of OpenSSF Scorecard are declared exceptions (ADR-2126).**
  `.config/lint-exceptions.d/scorecard-code-review.toml` and
  `scorecard-branch-protection.toml` name why Code-Review (alert 1) and
  Branch-Protection (alert 1054) cannot be met by a one-maintainer project that
  lands through the local merge train, and expire on 2027-03-31 or earlier. No
  ruleset or protection setting changes.
