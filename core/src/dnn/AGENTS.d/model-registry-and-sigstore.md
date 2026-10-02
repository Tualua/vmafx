---
paths:
  - model/tiny/registry.json
  - model/tiny/registry.schema.json
  - core/src/dnn/model_loader.c
invariant: Model registry entries must validate against schema and verify Sigstore bundles with cosign via posix_spawnp.
---
<!-- markdownlint-disable MD013 -->
# Model Registry Schema and Sigstore Verification

- **Registry schema is trust contract** (T6-9 / [ADR-0211](../../../../docs/adr/0211-model-registry-sigstore.md)).
  Every entry in [`model/tiny/registry.json`](../../../../model/tiny/registry.json)
  must satisfy [`registry.schema.json`](../../../../model/tiny/registry.schema.json):
  required `id` / `kind` / `onnx` / `sha256`, plus `license` and
  `sigstore_bundle` for `schema_version: 1` entries. New fields
  added by extending schema first, then registry, then any
  consumers — never other way around. `--tiny-model-verify`
  path in `model_loader.c` parses registry inline (no JSON dep),
  spawns `cosign` via `posix_spawnp(3p)`; `system(3)` is and stays
  banned.

- **Model registry + Sigstore (T6-9, PR #199 open, ADR-0211
  placeholder)**: `--tiny-model-verify` flag wires through to
  `cosign verify-blob` against Sigstore bundle declared in
  registry. Pairs with `quant_mode` / `int8_sha256`
  fields from
  [ADR-0173](../../../../docs/adr/0173-ptq-int8-audit-impl.md) /
  [ADR-0174](../../../../docs/adr/0174-first-model-quantisation.md).
  On merge: every shipped tiny-AI model needs Sigstore bundle
  path in `model/tiny/registry.json`.
