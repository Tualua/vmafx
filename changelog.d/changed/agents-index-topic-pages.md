- **A large subtree `AGENTS.md` is now a generated index over one page per
  topic.** `scripts/ci/AGENTS.md` (93,543 bytes) is the first: its text moved
  unchanged into 46 pages under `scripts/ci/AGENTS.d/`, and the file itself is
  a 14,133-byte index that tells an agent which pages to read for the paths
  it is about to touch. Measured on three tasks, an agent now loads 21% to 23%
  of what it loaded before. To record an invariant in such a directory, edit
  or add a page and run `make docs-fragments-write`;
  `make docs-fragments-check` fails on a stale index, on a page above 12,000
  bytes, on an index above 16,000 bytes and on a page whose path globs match
  no file. `scripts/docs/agents_migration_check.py` proves that a migration
  moved every paragraph and every identifier
  ([ADR-1454](docs/adr/1454-agents-index-and-topic-pages.md),
  [agents index and topic pages](docs/development/agents-index.md)).
