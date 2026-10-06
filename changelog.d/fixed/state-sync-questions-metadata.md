- **The post-commit state sync works in linked worktrees again.** The hook
  (`scripts/githooks/state-sync.sh`, ADR-1280) mirrored the private ledgers
  into a worktree without `questions.meta.json`, and Praetor's state sync
  reads it for every `QUESTIONS.md` entry, so every commit in a linked
  worktree ended its post-commit hook with `state sync list questions:
  QUESTIONS.md line 7: Q-001 metadata missing from questions.meta.json` and
  the worktree's state was not synchronised. The file is now mirrored with the
  others.
