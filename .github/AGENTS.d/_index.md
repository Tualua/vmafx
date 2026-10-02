# Agent notes — `.github/` (workflows + templates)

Parent: [../AGENTS.md](../../AGENTS.md).

This directory holds GitHub-facing config: Actions workflows, issue /
PR templates, CODEOWNERS file. Everything here is fork-local.
Netflix/vmaf upstream has its own `.github/` that rarely overlaps
path-wise. Conflicts on merge tend to be rare but high-impact
when they happen (silently-broken workflow is less visible than
broken `.c` file).

## Invariants a reviewer or sync must preserve
