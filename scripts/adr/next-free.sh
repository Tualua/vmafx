#!/usr/bin/env bash
# scripts/adr/next-free.sh — print (or atomically claim) the next available ADR number.
#
# Accounts for:
#   - ADR files already present in the local working tree
#   - ADR stub files (.md.stub) created by prior --claim calls in this session
#   - ADR files already merged into origin/master (cross-branch awareness)
#   - In-flight branches on origin that contain an ADR file at their tip
#
# Usage:
#   scripts/adr/next-free.sh
#   # -> e.g. "0532"   (read-only; prints next free number, does not claim)
#
#   scripts/adr/next-free.sh --claim <topic-slug>
#   # -> e.g. "0532"   (atomically creates docs/adr/0532-<topic-slug>.md.stub)
#   #                   and prints the reserved number.
#   #                   Parallel callers observe the stub and skip that number.
#   #                   Rename .stub -> .md when committing the real ADR.
#
#   scripts/adr/next-free.sh --release <NNNN>
#   # -> releases a previously claimed stub (e.g. if the PR is abandoned).
#
# Atomic claim guarantee (single machine, parallel callers):
#   Claims use a per-repo lock dir under /tmp (POSIX mkdir is atomic on
#   Linux ext4/tmpfs) to serialise concurrent invocations on the same host.
#   The stub file is the persistent cross-process signal; after the lock is
#   released the next caller sees the stub and skips the number.
#
# Remote-branch awareness:
#   With --claim, the script queries origin for any branch whose tip commit
#   touches docs/adr/NNNN-*.md (via git ls-remote + git ls-tree).  Numbers
#   claimed by in-flight branches are skipped.  This covers the parallel-agent
#   scenario where each agent pushes to its own branch before merging.
#
# See ADR-0386 (docs/adr/0386-adr-numbering-collision-prevention.md)
# and ADR-0535 (docs/adr/0535-adr-atomic-allocator.md).

set -euo pipefail

# ── constants ────────────────────────────────────────────────────────────────

REPO_ROOT="$(git rev-parse --show-toplevel)"
ADR_DIR="${REPO_ROOT}/docs/adr"

# Lock file is keyed by repo root so concurrent repos on the same host do not
# contend, but parallel agents in the same repo do serialize.
_repo_key="$(printf '%s' "${REPO_ROOT}" | tr '/' '_')"
LOCK_DIR="/tmp/vmaf_adr_claim_lock_${_repo_key}"

# ── helpers ───────────────────────────────────────────────────────────────────

usage() {
  printf 'Usage:\n' >&2
  printf '  scripts/adr/next-free.sh                   # print next free number (read-only)\n' >&2
  printf '  scripts/adr/next-free.sh --claim <slug>    # atomically reserve next number\n' >&2
  printf '  scripts/adr/next-free.sh --release <NNNN>  # release a previously claimed stub\n' >&2
  exit 1
}

# Acquire the per-repo lock using an atomic mkdir.
# Retries for up to 10 seconds, then fails loudly.
_acquire_lock() {
  local deadline
  deadline=$(($(date +%s) + 10))
  while ! mkdir "${LOCK_DIR}" 2>/dev/null; do
    if [ "$(date +%s)" -ge "${deadline}" ]; then
      printf 'ERROR: could not acquire ADR allocator lock (%s) within 10 s.\n' "${LOCK_DIR}" >&2
      printf '       Another process may be holding it.  Remove manually if stale.\n' >&2
      exit 1
    fi
    sleep 0.1
  done
  # Register a trap to release the lock on exit (including errors).
  trap '_release_lock' EXIT INT TERM
}

_release_lock() {
  rmdir "${LOCK_DIR}" 2>/dev/null || true
  trap - EXIT INT TERM
}

# Collect all taken 4-digit ADR numbers from local tree, stubs, origin/master,
# and any extra tree-ish refs passed as arguments (remote branch tips).
_collect_taken() {
  {
    # Local real ADR files
    ls "${ADR_DIR}"/[0-9][0-9][0-9][0-9]-*.md 2>/dev/null || true
    # Local stub files (cross-process claim markers)
    ls "${ADR_DIR}"/[0-9][0-9][0-9][0-9]-*.md.stub 2>/dev/null || true
    # origin/master
    git ls-tree -r --name-only origin/master docs/adr/ 2>/dev/null |
      grep -E '^docs/adr/[0-9]{4}-' || true
    # Extra trees (e.g. remote branch tip SHAs)
    for extra_tree in "$@"; do
      git ls-tree -r --name-only "${extra_tree}" docs/adr/ 2>/dev/null |
        grep -E '^docs/adr/[0-9]{4}-' || true
    done
  } |
    sed 's|.*/||' |
    grep -oE '^[0-9]{4}' |
    sort -u
}

# Given a sorted unique list of taken numbers on stdin, print the next free one.
_next_free_from_taken() {
  local -a taken=()
  while IFS= read -r n; do
    taken+=("${n}")
  done
  if [ "${#taken[@]}" -eq 0 ]; then
    printf '0001\n'
    return
  fi
  # Highest taken + 1.  IDs are never reused (per ADR-0386).
  local highest="${taken[-1]}"
  printf '%04d\n' $((10#${highest} + 1))
}

# Fetch tip SHAs of every remote branch (except master) that contains at least
# one docs/adr/NNNN-*.md entry.  Prints one SHA per line.  Best-effort only.
_remote_branch_trees() {
  git ls-remote --heads origin 2>/dev/null |
    awk '{print $1, $2}' |
    grep -v 'refs/heads/master$' |
    while IFS=' ' read -r sha _ref; do
      if [ -n "${sha}" ]; then
        if git ls-tree -r --name-only "${sha}" docs/adr/ 2>/dev/null |
          grep -qE '^docs/adr/[0-9]{4}-'; then
          printf '%s\n' "${sha}"
        fi
      fi
    done
}

# Write the stub frontmatter for a newly claimed ADR.
_write_stub() {
  local stub_path="$1" number="$2" slug="$3"
  local ts dt
  ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  dt="$(date -u +%Y-%m-%d)"
  {
    printf '<!-- ADR-%s stub — claimed by scripts/adr/next-free.sh --claim %s on %s -->\n' \
      "${number}" "${slug}" "${ts}"
    printf '<!-- Replace this file with the real ADR and rename to %s-%s.md before committing. -->\n' \
      "${number}" "${slug}"
    printf '<!-- To abandon this claim run: scripts/adr/next-free.sh --release %s -->\n' \
      "${number}"
    printf '\n# ADR-%s: <fill in title>\n\n' "${number}"
    printf -- '- **Status**: Proposed\n'
    printf -- '- **Date**: %s\n' "${dt}"
    printf -- '- **Deciders**: <fill in>\n'
    printf -- '- **Tags**: <fill in>\n'
    printf '\n## Context\n\n<fill in>\n\n'
    printf '## Decision\n\n<fill in>\n\n'
    printf '## Alternatives considered\n\n'
    printf '| Option | Pros | Cons | Why not chosen |\n'
    printf '|---|---|---|---|\n'
    printf '| | | | |\n\n'
    printf '## Consequences\n\n'
    printf -- '- **Positive**: <fill in>\n'
    printf -- '- **Negative**: <fill in>\n'
    printf -- '- **Neutral / follow-ups**: <fill in>\n\n'
    printf '## References\n\n'
    printf -- '- See [ADR-0535](0535-adr-atomic-allocator.md) for the allocator design.\n'
    printf -- '- Source: <req or Q<round>.<q>>\n'
  } >"${stub_path}"
}

# ── argument parsing ──────────────────────────────────────────────────────────

MODE="query"
SLUG=""
RELEASE_NUM=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --claim)
      MODE="claim"
      shift
      if [ "$#" -eq 0 ] || [[ "$1" == --* ]]; then
        printf 'ERROR: --claim requires a topic-slug argument (e.g. my-adr-topic).\n' >&2
        usage
      fi
      SLUG="$1"
      shift
      ;;
    --release)
      MODE="release"
      shift
      if [ "$#" -eq 0 ] || [[ "$1" == --* ]]; then
        printf 'ERROR: --release requires a 4-digit ADR number (e.g. 0532).\n' >&2
        usage
      fi
      RELEASE_NUM="$1"
      shift
      ;;
    -h | --help)
      usage
      ;;
    *)
      printf 'ERROR: unknown argument: %s\n' "$1" >&2
      usage
      ;;
  esac
done

cd "${REPO_ROOT}"

# ── release mode ──────────────────────────────────────────────────────────────

if [ "${MODE}" = "release" ]; then
  found=()
  while IFS= read -r f; do
    found+=("${f}")
  done < <(find "${ADR_DIR}" -maxdepth 1 -name "${RELEASE_NUM}-*.md.stub" 2>/dev/null | sort)
  if [ "${#found[@]}" -eq 0 ]; then
    printf 'ERROR: no stub file for %s found in %s.\n' "${RELEASE_NUM}" "${ADR_DIR}" >&2
    exit 1
  fi
  for f in "${found[@]}"; do
    rm -f "${f}"
    printf 'released %s (removed %s)\n' "${RELEASE_NUM}" "${f}" >&2
  done
  printf 'released %s\n' "${RELEASE_NUM}"
  exit 0
fi

# ── query mode (read-only) ────────────────────────────────────────────────────

# Fetch the latest master tip (soft failure on network outage or offline dev).
git fetch origin master --depth=50 --quiet 2>/dev/null || true

if [ "${MODE}" = "query" ]; then
  _collect_taken | _next_free_from_taken
  exit 0
fi

# ── claim mode (atomic) ───────────────────────────────────────────────────────

# Validate slug before taking the lock.
if ! printf '%s' "${SLUG}" | grep -qE '^[a-z0-9][a-z0-9-]*$'; then
  printf 'ERROR: slug must match [a-z0-9][a-z0-9-]* (got: %s).\n' "${SLUG}" >&2
  exit 1
fi

# Network fetch outside the lock to avoid holding it during slow I/O.
git fetch origin --depth=50 --quiet 2>/dev/null || true

# Query remote branch tip trees (best-effort; soft failure).
remote_trees=()
while IFS= read -r tree; do
  remote_trees+=("${tree}")
done < <(_remote_branch_trees 2>/dev/null || true)

# Acquire the exclusive per-repo lock.
_acquire_lock

# Re-collect inside the lock (another process may have claimed between the
# fetch above and our lock acquisition).
number="$(_collect_taken "${remote_trees[@]}" | _next_free_from_taken)"

stub_path="${ADR_DIR}/${number}-${SLUG}.md.stub"

_write_stub "${stub_path}" "${number}" "${SLUG}"

# Release the lock early to minimise critical-section duration.
_release_lock

printf '%s\n' "${number}"
