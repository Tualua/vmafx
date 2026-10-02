---
paths:
  - core/tools/cli_parse.cpp
  - core/tools/cli_parse.h
invariant: Default precision is %.6f; --precision=max opts in to %.17g; --tiny-model passes string through unchanged.
---
# Precision defaults and tiny-AI model surfaces

- **Default numeric precision is `%.6f`** (Netflix-compatible — required by
  CLAUDE.md §8 golden gate). `--precision=max` (alias `full`) opts in to
  `%.17g` (IEEE-754 round-trip lossless). `--precision=N` overrides with
  `"%.<N>g"`; `--precision=legacy` is preserved as synonym for default.
  See [ADR-0119](../../../docs/adr/0119-cli-precision-default-revert.md)
  (supersedes [ADR-0006](../../../docs/adr/0006-cli-precision-17g-default.md)).
  Applies to both stderr and file outputs (XML / JSON / CSV / sub-XML).
- **`--tiny-model PATH`** loads ONNX checkpoint via
  [src/dnn/](../../src/dnn/AGENTS.md). Path resolved via `realpath` inside
  loader; CLI passes string through unchanged. See
  [ADR-0023](../../../docs/adr/0023-tinyai-user-surfaces.md).

- [ADR-0119](../../../docs/adr/0119-cli-precision-default-revert.md) — `%.6f`
  default (Netflix-compat) + `--precision=max` for round-trip lossless.
  Supersedes ADR-0006.
- [ADR-0006](../../../docs/adr/0006-cli-precision-17g-default.md) — *Superseded.*
  Original `%.17g`-default decision; kept for history.
- [ADR-0023](../../../docs/adr/0023-tinyai-user-surfaces.md) — `--tiny-model`
  as one of four tiny-AI surfaces.
