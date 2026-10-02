---
paths:
  - ai/scripts/train_vmaf_tiny_v*.py
  - ai/scripts/export_vmaf_tiny_v*.py
  - model/tiny/vmaf_tiny_v*.onnx
invariant: v2 mlp_small is default; v3 medium and v4 large ship alongside; never modify earlier tier scripts.
---
<!-- markdownlint-disable MD013 MD060 -->
# vmaf_tiny MLP architecture tiers

- **`vmaf_tiny_v2` ONNX contract (ADR-0244).** Shipped ONNX
  embeds StandardScaler `(mean, std)` as Constant `Sub` + `Div`
  nodes running before MLP. Runtime feeds raw canonical-6
  feature values; do NOT add external scaler step. Re-exporting
  via [`ai/scripts/export_vmaf_tiny_v2.py`](../scripts/export_vmaf_tiny_v2.py)
  = only supported path — pulls `mean` / `std` from
  trainer checkpoint, bakes them as graph initialisers, so
  `model/tiny/registry.json` sha256 covers calibration values
  too. Input name = `features` ([N, 6] float32), output `vmaf`
  ([N] float32); feature column order fixed at
  `(adm2, vif_scale0, vif_scale1, vif_scale2, vif_scale3, motion2)`
  and must not be reordered without full Phase-3 re-validation.
- **`vmaf_tiny_v3` ships alongside v2 (ADR-0241).** Same ONNX
  contract as v2 (input `features [N, 6]` float32, output
  `vmaf [N]` float32, opset 17, scaler-baked-into-graph) — only
  architecture differs (`mlp_medium` 6 → 32 → 16 → 1, 769 params vs
  v2's `mlp_small` 257). **Production default stays v2**;
  [`docs/ai/inference.md`](../../docs/ai/inference.md) and model-card
  cross-references both keep v2 as recommended `--tiny-model`.
  v3 = higher-PLCC / lower-variance option (Netflix LOSO mean
  PLCC 0.9986 ± 0.0015 vs v2's 0.9978 ± 0.0021). Do NOT replace v2
  with v3 wholesale. Both file paths referenced by name in
  user-facing docs and registry; small mean delta does not justify
  default flip without multi-seed + KoNViD 5-fold parity (documented
  as Phase-3e follow-up). Same scripts pattern:
  `train_vmaf_tiny_v3.py` / `export_vmaf_tiny_v3.py` /
  `validate_vmaf_tiny_v3.py` / `eval_loso_vmaf_tiny_v3.py` —
  do **not** modify v2 scripts when iterating on v3.
- **`vmaf_tiny_v3` and `vmaf_tiny_v4` opt-in tiers
  (ADR-0241 / ADR-0242).** v3 (`mlp_medium`, 769 params, ADR-0241)
  and v4 (`mlp_large`, 3 073 params, ADR-0242) ship *alongside* v2,
  not as replacements. Production default stays `vmaf_tiny_v2`. Three
  rungs share canonical-6 input contract, bundled
  StandardScaler, and 90 ep / Adam@1e-3 / MSE / bs=256 recipe;
  only architecture differs. **Do NOT modify v2 or v3 scripts
  when iterating on later rungs** — each version owns its own
  `train_vmaf_tiny_vN.py` / `export_vmaf_tiny_vN.py` /
  `validate_vmaf_tiny_vN.py` / `eval_loso_vmaf_tiny_vN.py` quartet.
  Arch ladder **stops at v4**: v3 → v4 LOSO PLCC delta =
  +0.0001 (well below 1 std), demonstrating saturation on
  canonical-6 + 4-corpus regime. Future quality gains require
  regime change (richer features, larger corpus, ensembles), not
  deeper MLPs. See ADR-0242 § Alternatives considered for
  mlp_huge rejection rationale.
