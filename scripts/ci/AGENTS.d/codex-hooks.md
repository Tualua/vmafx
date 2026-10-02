---
paths:
  - .codex/hooks.json
  - .codex/hooks/*
  - scripts/ci/tests/test_codex_hook_config.py
invariant: Every Codex hook command resolves through the active worktree root after clearing `GIT_DIR` / `GIT_WORK_TREE`.
---
<!-- markdownlint-disable MD013 MD060 -->
# Codex repository-hook path contract

`.codex/hooks.json` must invoke every tracked hook through the active Git
worktree root after clearing inherited repository-local Git variables. The
command contract starts with `"$(env -u GIT_DIR -u GIT_WORK_TREE` and ends with
`git rev-parse --show-toplevel)/.codex/hooks/<script>.sh"`.
Never commit a user-home path, a former checkout path, or a path relative to the
launch directory. Keep the exact seven event/matcher/script mappings, tracked
`100755` modes, and `scripts/ci/tests/test_codex_hook_config.py` together. The
`test-codex-hook-config` pre-commit/pre-push hook is the required local and CI
caller; a JSON parse alone does not prove that the commands execute from a
nested directory or a linked worktree.
