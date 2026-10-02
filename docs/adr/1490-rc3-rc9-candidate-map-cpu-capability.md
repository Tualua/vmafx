<!-- markdownlint-disable MD013 MD060 -->
# ADR-1490: Insert RC7 CPU capability source of truth and shift the later candidates

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: release, rc, process, simd

## Context

[ADR-1421](1421-rc3-rc8-candidate-map.md) mapped the first-release candidates to
RC3 to RC8, with RC6 owning a generated GPU capability table and RC7 owning
benchmarks, profiling and tuning. It has no candidate for the CPU side of the
same question.

Every AVX-512 result of the fork was produced on one AMD Zen 5 processor.
Intel Xeons and Apple Silicon machines run the same dispatch without anyone
having verified it. Nothing in the tree records which SIMD kernel needs which
CPU feature, and nothing checks the record. The inputs are spread over the
per-translation-unit compile flags in `core/src/meson.build`, the runtime gates
in `core/src/x86/cpu.c` and `core/src/arm/cpu.c`, and the dispatch sites of the
extractors. An outside contributor raised the gap. The first look at the tree
confirms it: AVX2 translation units are compiled with `-mfma` while the AVX2
gate does not test for FMA, and the aarch64 SVE2 gate does not test the vector
length.

RC6 answers the same question for GPUs: a checked-in table generated from the
vendor toolchains, with an all-target audit that needs no device. The CPU case
needs the CPU equivalent, and it can be verified without owning the hardware:
Intel SDE and qemu emulate CPU models, and `objdump` lists the instructions of
every function.

## Decision

We will insert a new candidate, **RC7: CPU capability source of truth**, and
move the two later candidates up by one. This ADR replaces the candidate
numbering of ADR-1421 from RC7 on; RC1 to RC6 are unchanged.

| Candidate | Owns | Epic |
| --- | --- | --- |
| `v1.0.0-rc.1` to `v1.0.0-rc.6` (RC1 to RC6) | Unchanged from ADR-1421. | - |
| `v1.0.0-rc.7` (RC7) | CPU capability source of truth, defined below. | [#1885](https://github.com/VMAFx/vmafx/issues/1885) |
| `v1.0.0-rc.8` (RC8) | Benchmarks, profiling and tuning (formerly RC7; ADR-1341's benchmark evidence, pinned to the rc.8 artifact). | [#1245](https://github.com/VMAFx/vmafx/issues/1245) |
| `v1.0.0-rc.9` (RC9) | The one-shot real retrain and the remaining tiny-AI training (formerly RC8; ADR-1341's retrain evidence, pinned to the rc.9 artifact). | [#1246](https://github.com/VMAFx/vmafx/issues/1246), [#1242](https://github.com/VMAFx/vmafx/issues/1242) |

Final `v1.0.0` follows accepted RC9 evidence and any required repair candidate.

Statements of ADR-1421 that this ADR replaces:

- the table rows for RC7 and RC8, which become the RC8 and RC9 rows above;
- "Final `v1.0.0` follows accepted RC8 evidence" (now RC9);
- "Benchmarking and tuning stay out of RC3 to RC6. Training never starts before
  RC7 evidence is accepted" (now: benchmarks and tuning never in RC3 to RC7;
  training never before RC8 evidence is accepted);
- "recovered in RC7" for speed lost to exactness in RC3 (now RC8);
- "Benchmarks come last before training" in the ordering rationale (the CPU
  capability candidate now sits between the GPU capability table and the
  benchmarks);
- the statement that the `tools/rc1-tester` catalog labels are `RC7` and `RC8`
  (they are `RC8` and `RC9`), and the names `T-RC2-BENCH-TUNE` and
  `T-RC3-MODEL-RETRAIN`, which still keep their identity.

Everything else in ADR-1421 and ADR-1341 stays.

### RC7 exit bar

RC7 is accepted when all of the following hold on the exact candidate head:

1. **Table.** A generated, checked-in table of the CPU features each SIMD
   kernel needs, derived from the per-translation-unit compile flags in
   `core/src/meson.build` and the runtime gates in `core/src/x86/cpu.c` and the
   arm64 equivalent. One script regenerates it, and CI fails when the checked-in
   copy is stale.
2. **Audit.** A disassembly audit finds no kernel that contains an instruction
   outside the feature set its runtime gate guarantees, checked per function,
   for x86 and for aarch64.
3. **Emulated matrix.** Every dispatch level runs bit-exact against the scalar
   code under emulation: Intel SDE CPU models for x86 (an AVX2-only model,
   Skylake-X, Ice Lake, Sapphire Rapids, and the AMD AVX-512 set) and qemu for
   aarch64 (NEON, and SVE2 at more than one vector length).
4. **Real hardware is extra evidence.** Reports from real Xeon or Apple
   Silicon machines count as additional evidence, not as a requirement. Timing
   is not part of RC7: it belongs to RC8.

### Ordering rules

Benchmarks and tuning never happen in RC3 to RC7. Training never starts before
RC8 evidence is accepted. Speed lost to exactness in RC3 is recorded as a tuning
row and recovered in RC8, never traded back for a tolerance. A candidate that
finds a correctness regression fixes it and reruns the affected evidence before
the next candidate proceeds.

Ordering rationale: tuning starts from a verified dispatch table. RC8 then
measures kernels whose feature requirements are recorded and whose dispatch
levels are known to be bit-exact, instead of tuning a path that a processor
might not be able to run.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
| --- | --- | --- | --- |
| Widen RC6 to cover GPU and CPU capability | No new candidate; one capability story. | RC6 already needs generators for three GPU vendors and an all-target compile matrix; the CPU work needs different tools (Intel SDE, qemu, per-function disassembly) and a different exit evidence, so the candidate would close late and carry two unrelated bars. | One candidate, one checkable exit bar (the rule ADR-1421 set). |
| Append the CPU work as RC9 after the retrain | Does not renumber anything. | The retrain and the benchmarks would run on a dispatch nobody verified; a CPU-feature defect found afterwards invalidates both. | Violates the rule that no work leaks across phases and that evidence is taken on a settled tree. |
| Insert as RC7 and shift the rest (**chosen**) | Tuning starts from a verified dispatch table; mirrors RC6; training stays last. | Renumbers RC7 and RC8 in every document, issue and ledger row; the final release is one candidate further away. | Chosen by the maintainer. |
| Exit bar: table only | Smallest scope. | A table records intent; it does not show that a kernel obeys it or that a processor runs it. | The audit and the emulated matrix are what make the table checkable. |
| Exit bar: table and audit without emulation | No SDE or qemu setup. | Static audit says nothing about bit-exactness on a CPU model; Intel AVX-512 stays unrun. | Real Xeons are not available to the project; emulation covers them. |
| Exit bar: table, audit and emulated matrix (**chosen**) | Covers kernels' needs, gates and results for CPU models nobody owns. | Needs Intel SDE (not installed on the development host) and qemu models; emulated runs are slow. | Chosen by the maintainer; real-hardware reports stay optional evidence. |

## Consequences

- **Positive**: The CPU dispatch gets the same source-of-truth treatment as the
  GPU dispatch. Gaps such as AVX2 kernels built with `-mfma` behind a gate that
  does not test FMA have an owner and a closure condition. Benchmarks and
  tuning (RC8) start from a verified dispatch table.
- **Negative**: The final release is one candidate further away. Historical
  records keep the numbering of their date and must be read with the map in
  force then: ADR-1421's body, earlier `docs/rebase-notes.md` entries,
  `CHANGELOG.md`, closed ledger rows and `docs/state.md` update notes dated
  before 2026-10-02. A branch that opened a tuning row as RC7 or a training row
  as RC8 relabels it RC8 or RC9.
- **Neutral / follow-ups**: The epics follow the map (#1885 is new; #1245 is RC8;
  #1246 and #1242 are RC9). The release guide, roadmap, retrain runbook, tester
  guide, model card, dependency-bot policy, ledger classification, `AGENTS.md`
  section 11 with its compiled projections and the `tools/rc1-tester`
  inventory use the new numbering in the same change. The RC7 table's own
  documentation page and generator are RC7's deliverables, not this change's.
  This decision adds no dependency, build-time fetch, runtime surface or SBOM
  component.

## References

- `req` (verbatim): "i think we should add one last rc lol"
- `req` (verbatim): "well so i am missing a twin rc6 for cpu abilities"
- `Q` (popup, 2026-10-02, placement, verbatim): "New RC7, shift the rest (Recommended)"
- `Q` (popup, 2026-10-02, exit bar, verbatim): "Table, audit and emulated matrix (Recommended)"
- [ADR-1421](1421-rc3-rc8-candidate-map.md) — the map whose numbering from RC7 on this ADR replaces.
- [ADR-1341](1341-rc-correctness-benchmark-retrain-sequence.md) — the evidence rules this ADR keeps.
- [ADR-1352](1352-rc-phase-shift-plus-one.md) — the earlier mapping, already superseded.
- Epics [#1885](https://github.com/VMAFx/vmafx/issues/1885), [#1725](https://github.com/VMAFx/vmafx/issues/1725), [#1245](https://github.com/VMAFx/vmafx/issues/1245), [#1246](https://github.com/VMAFx/vmafx/issues/1246) and [#1242](https://github.com/VMAFx/vmafx/issues/1242).
