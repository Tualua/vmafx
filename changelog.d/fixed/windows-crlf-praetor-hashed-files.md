- **A Windows checkout passes the governance hooks on unmodified files.** The
  archetypes `.standards.lock` pins, `.standards.*`, `AGENTS.md`, its compiled
  agent-context files and the agent personas are now checked out with LF on
  every platform (`.gitattributes`). `* text=auto` had given them CRLF on
  Windows even with `core.autocrlf=false`, so the `hiss-audit` hook failed the
  lockfile digest and `context-check` reported `CLAUDE.md` out of sync. The
  Windows setup guide in `docs/development/pre-commit-hooks.md` now clones with
  `core.eol=lf` as well.
