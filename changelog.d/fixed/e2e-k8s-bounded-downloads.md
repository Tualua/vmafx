- **The Kubernetes E2E workflow bounds its tool downloads and stops hiding
  failed diagnostics.** The kind, kubectl, Helm and kuttl downloads had
  retries but no deadline, so a stalled server held the job until its
  45-minute limit; each now has `--max-time 300`. The failure diagnostics
  discarded every error with `|| true` and `2>/dev/null`; each command now
  reports a warning when it fails and the next one still runs.
