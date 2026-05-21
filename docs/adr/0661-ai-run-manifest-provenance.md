# ADR-0661: AI run manifest provenance

- **Status**: Accepted
- **Date**: 2026-05-20
- **Deciders**: Lusoris maintainers
- **Tags**: ai, tooling, manifests, training

## Context

The fork is refreshing many AI-derived artifacts at once: Netflix-derived
regressors, KonViD/CHUG MOS heads, saliency and second-opinion materializers,
and codec-profile experiments. Several scripts already emit model sidecars, but
the run identity is inconsistent: a CHUG-facing command can delegate to the
shared KonViD trainer, input paths may be local-only, and CLI arguments are not
recorded in a shared shape.

We need enough manifest provenance to reproduce a local training run or promote
its output into a model card without turning gitignored local corpus paths into
a CI contract.

## Decision

We will add a shared `aiutils.run_manifest` helper and have MOS-head trainers
emit a `run_provenance` block in their JSON sidecars. The block records a schema
name, user-facing entrypoint script with SHA-256, normalized CLI arguments,
named input/output paths, and file hashes where the path exists. CHUG runs keep
`train_chug_hdr_mos_head.py` as the entrypoint while also recording the shared
trainer implementation that wrote the sidecar.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep one-off manifest JSON in each trainer | Minimal diff; no helper import | Repeats path hashing and argument normalization; CHUG/KonViD identity can drift again | The active backlog is explicitly about consolidating AI script-family plumbing |
| Store full environment snapshots | Captures more state | Leaks noisy host details, grows sidecars, and makes local-only runs look like reproducibility guarantees | The useful contract is input/output/config provenance, not a full machine image |
| Require a single config file for every run before adding provenance | Cleaner long-term config story | Blocks current HDR/CHUG refresh work and does not help existing command-line runs | Provenance is incremental and can later point at config files when those land |

## Consequences

- **Positive**: local MOS-head artifacts can be traced back to the command,
  script revision, input files, and output targets that produced them.
- **Positive**: CHUG manifests stay CHUG-named even though the implementation
  shares the KonViD training loop.
- **Negative**: sidecars become slightly larger and include local path names.
- **Neutral / follow-ups**: broader `train_` / `export_` / `eval_` /
  `validate_` script families can adopt the helper as they move from ad hoc CLI
  state toward shared config plumbing.

## References

- Research: [Research-0661](../research/0661-ai-run-manifest-provenance.md)
- Related: [ADR-0658](0658-project-modernization-audit.md)
- Source: req: "well go on i guess we have enough backlog..."
