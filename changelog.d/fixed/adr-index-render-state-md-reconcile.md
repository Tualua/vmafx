- ADR index gate (`make lint`): render the pending `docs/adr/_index_fragments/`
  rows into `docs/adr/README.md` via `scripts/docs/concat-adr-index.sh --write`,
  greening `concat-adr-index.sh --check`. Adds the ADR-1119 (golusoris fx
  framework) full row plus 9 older fragment rows (0452, 0460, 0539, 0567, 0764,
  0866, 0982, 0993) that had fragment files but were not yet rendered. The
  ~161-row `_index_fragments` backfill (CLAUDE.md §12 r8) remains a deliberate
  follow-up.
- `docs/state.md` reconciliation (CLAUDE.md §12 r13): move four already-merged
  bugs from "Open bugs" to "Recently closed" with PR/commit + ADR cross-links —
  T-THREADED-MULTI-PREV-REF-STARVATION (PR #906, ADR-1107),
  T-HIP-MOTION-V2-MIRROR-OFF-BY-ONE (PR #905, ADR-1106),
  T-RC-CI-GREENUP (PR #912), T-CI-APT-MS-REPO-FLAKE (PR #903).
