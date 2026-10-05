<!-- markdownlint-disable MD013 MD060 -->
# ADR-1805: Delete the Netflix release tags inherited by VMAFx/vmafx

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: Lusoris
- **Tags**: release, go, git, supply-chain

## Context

VMAFx/vmafx was created from a copy of Netflix/vmaf and carried 26 of its tags
(`v1.0.2` to `v1.5.3`, `v1.3.6rc`, `v1.3.7rc`, `v2.0.0` to `v2.3.1`, `v3.0.0-rc`,
`v3.0.0`). Two defects follow from them ([ADR-1127](1127-single-semver-release-stream.md)
describes the release stream they collide with).

First, Go consumers. `go get github.com/VMAFx/vmafx@latest` resolved to
Netflix's `v3.0.0+incompatible`. The Go command reads retractions only from the
`go.mod` of the highest version, and Netflix's `v1.5.3`, which has no
`go.mod`, ranked above every fork `v1.0.x`; a `retract [v1.0.0-rc.1,
v1.0.0-rc.2]` in the fork's `go.mod` (the content of PR #2104) therefore has no
effect. Measured with Go 1.27.1 against a mirror of the repository's tag set
with `GOPROXY=direct`: with every inherited tag present, `go list -m
-versions` and `-retracted` list all 26 versions plus the release candidates
and `@latest` is `v3.0.0+incompatible`; a `v1.0.0` carrying the retract changes
nothing; with `v1.0.2` to `v1.5.3` deleted, `go list -m -versions` is `v1.0.0`,
`-retracted` adds the two candidates, `@latest` is `v1.0.0`, and a consumer
pinned to a candidate is shown `(retracted)`.

Second, name collisions. The fork's own stream reaches `v1.0.2`, `v1.1.x`,
`v1.2.0` and `v1.3.x`, which already exist as Netflix tags; GitHub's create-release
API ignores `target_commitish` when the tag exists, so a release `v1.0.2` would
attach to Netflix's commit.

## Decision

The maintainer decided on 2026-10-05 to delete every inherited tag from
VMAFx/vmafx. A tag is deleted only when Netflix/vmaf defines a tag of the same
name that points at the same object, after its name, object, commit and
Netflix's object are recorded in `scripts/release/inherited-upstream-tags.json`
and [the record](../development/inherited-tags.md). The deletion runs through
`scripts/release/delete-inherited-upstream-tags.py` (dry run by default,
`--apply` deletes with `DELETE repos/VMAFx/vmafx/git/refs/tags/<name>`, a tag
whose object differs from Netflix's is refused). Commits stay in history. Two
guards keep the tags out: the lefthook `pre-push` command
`scripts/git-hooks/check-push-tags.py` refuses a pushed tag equal to a recorded
Netflix tag or named outside the fork's patterns, and the `upstream` remote is
set to `tagOpt = --no-tags`.

Result on 2026-10-05: 26 tags deleted; `v1.0.0-rc.1`, `v1.0.0-rc.2`, three
`tester-*`, `archive/eupl-relicense-v1` and `tiny-blobs-v1` stay; no GitHub
release object was attached to a deleted tag. `GOPROXY=direct go list -m
-versions github.com/VMAFx/vmafx` prints `v1.0.0-rc.1 v1.0.0-rc.2` and `@latest`
`v1.0.0-rc.2`. `proxy.golang.org` still answered `@latest = v3.0.0+incompatible`
and still listed the Netflix versions: it caches and has no purge, so Go
consumers pin an explicit version until its `@latest` shows a fork version.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Delete all inherited tags (chosen) | Fixes git-direct consumers at once, removes the name collisions, lets a `retract` on `v1.0.0` take effect, keeps ADR-1127's stream | Destructive on shared refs; the proxy keeps cached versions | Chosen. |
| Delete only `v1.x` | Fewest refs touched; `v1.0.2` to `v1.5.3` are the colliding names | Without `v1.x` tags and `go.mod` above them Go still lists `v2`/`v3` `+incompatible` versions as `@latest` candidates; Netflix v2/v3 names stay on the Tags page | Leaves a half-clean tag list for one fewer deleted ref. |
| Keep tags, release `v1.5.4` with a `retract` for the Netflix ranges | Works through the proxy without deleting; measured: `@latest = v1.5.4`, all `+incompatible` versions vanish | Breaks ADR-1127's stream from `v1.0.0`; fork `v1.0.x` to `v1.5.3` would sit inside a retracted range; collisions remain | Contradicts the release stream. |
| Go module in a subdirectory with prefixed tags (`gomod/v1.0.0`) | Measured working, independent of Netflix tags | Moves `go.mod`, changes every import path, Dockerfile and release tool; a second tag series; the root path still resolves to Netflix | High cost for a defect a deletion removes. |
| Keep and document the pin | No repository risk | `@latest` stays wrong, the retract stays inert, collision at `v1.0.2` stays | Does not fix either defect. |

## Consequences

- **Positive**: the repository's tag list is the fork's; the `retract` of PR #2104
  works once `v1.0.0` is tagged; release-please can use any `v1.x` name.
- **Negative**: anyone pinned to a Netflix version through this repository's
  tags loses the ref (none known; the fork's `go.mod` was never Netflix's);
  `proxy.golang.org` keeps cached versions and its `@latest` until it refreshes
  (timing unmeasured, no purge).
- **Neutral / follow-ups**: carry `retract [v1.0.0-rc.1, v1.0.0-rc.2]` forward in
  every later `go.mod`; re-check `go list -m -retracted` once `v1.0.0` exists;
  upstream syncs fetch without tags.

## References

- `Q: "Delete all inherited tags (Recommended)"` (maintainer popup, 2026-10-05).
- Go modules reference, `retract`: <https://go.dev/ref/mod#go-mod-file-retract>.
- [Release guide, Go consumers](../development/release.md#go-consumers-and-the-inherited-tags).
- [The record of removed tags](../development/inherited-tags.md).
