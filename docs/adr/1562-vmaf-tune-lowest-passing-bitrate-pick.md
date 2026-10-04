<!-- markdownlint-disable MD013 MD060 -->
# ADR-1562: Every vmaf-tune command returns the lowest-bitrate encode that meets the target

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: tools, vmaf-tune, python, go, breaking, fork-local

## Context

`vmaf-tune` had two pick rules for "the encode that meets a target VMAF".
`recommend --target-vmaf` (Python `pick_target_vmaf`, Go `PickTargetVMAF` and
`SmallestPassingCRF`), the ladder's default sampler and the `fast` objective
returned the smallest CRF whose VMAF cleared the target, which is the highest
bitrate among the passing rows. `compare` and the Phase B bisect, `benchmark`
and `prefilter` returned the lowest bitrate that cleared it. Both were
documented as they behaved (state row
`T-VMAFTUNE-PICK-SEMANTICS-DIVERGE-2026-10-04`). The `fast` objective,
`|vmaf - target| + 1e-4 * kbps`, also did not return a passing encode at all
when a CRF just under the target sat closer to it than the cheapest passing
one.

## Decision

Every command returns the lowest-bitrate encode whose VMAF meets the target.
One implementation per language holds the rule: Python
`vmaftune.recommend.lowest_passing_row`, Go `recommend.lowestPassing`. Ties go
to the higher VMAF, then to the lower CRF. A row without a usable
`bitrate_kbps` is an error, never skipped or ranked last. When no row meets
the target the closest miss is still returned, tagged `(UNMET)`.

- `recommend` (corpus and live mode, with and without `--with-uncertainty`)
  and the ladder's default sampler call that implementation. The interval-aware
  search walks the rows in ascending bitrate order, so its first tight
  clearing row is the lowest-bitrate one.
- The live-mode picker `_smallest_passing_crf` is replaced by
  `_lowest_bitrate_passing` (Python) and `SmallestPassingCRF` by
  `LowestBitratePassing` (Go), both thin groupers over the shared rule.
- The `fast` TPE objective scores a CRF that meets the target by its predicted
  bitrate and a CRF that misses it by `1e9 + shortfall`, so the search returns
  the cheapest CRF that meets the target (`objective_value` /
  `objectiveValue`, pinned value for value in both languages).

This is a breaking change of behaviour (`feat!`, `Migration:` footer).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Lowest passing bitrate everywhere (chosen) | One rule across `recommend`, `compare`, the ladder, `fast`, the bisect; the answer a user asking for "VMAF at least T" wants | Changes every `recommend` result and the ladder rungs; assumes the bitrate column is present | Chosen by the maintainer (popup 2026-10-04) |
| Smallest passing CRF everywhere | Matches the old `recommend` text; best quality that clears the gate | The highest bitrate among passing rows; the bisect and `compare` already rank the other way; CRF order means nothing across codecs whose quality knob rises with quality | Opposite of what the bisect, `compare` and `prefilter` already do |
| Keep both, document the split | No behaviour change | Two rules for one phrase; the divergence stays | The ledger row asked for a decision |
| Closest to the target from above in VMAF | Avoids overshooting quality | Not the cheapest encode; ties between codecs ambiguous | Not what the maintainer asked for |

## Consequences

- **Positive**: one rule in two languages, tested per command; `fast` now
  returns an encode that meets the target when one exists.
- **Negative**: `recommend`, the ladder rungs and `fast` return different
  CRFs than before (a lower-quality, cheaper one). Scripts that read the old
  pick must rerun. Corpus rows without `bitrate_kbps` now fail `recommend`.
- **Neutral / follow-ups**: `prefilter`'s objective keeps its own
  `|vmaf - target| + 1e-4 * kbps` form; it is not a pick over rows.

## References

- `Q` (maintainer popup, 2026-10-04, paraphrased): pick rule "lowest passing
  bitrate (recommended)": every command returns the lowest-bitrate encode that
  meets the target; `recommend`, the ladder sampler and `fast` change.
- State row `T-VMAFTUNE-PICK-SEMANTICS-DIVERGE-2026-10-04`.
- [ADR-0306](0306-vmaf-tune-coarse-to-fine.md),
  [ADR-0307](0307-vmaf-tune-ladder-default-sampler.md).
