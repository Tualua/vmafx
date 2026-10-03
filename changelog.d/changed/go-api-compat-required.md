- The Required Checks Aggregator now requires praetor's `Go API Compatibility`
  check (`required` list). `scripts/ci/check-aggregator-names.sh` gained the
  `# required-aggregator-job: <name>` marker, which fails when no workflow job
  reports the name, so a rename of a job in a byte-locked workflow can no
  longer pass the name check.
