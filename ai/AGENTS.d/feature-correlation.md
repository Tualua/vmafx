---
paths:
  - ai/scripts/feature_correlation.py
  - ai/src/vmaf_train/eval.py
invariant: Feature-correlation JSON is finite-only; NaN propagation guards in correlations return plcc=0, srocc=0.
---
<!-- markdownlint-disable MD013 MD060 -->
# Feature correlation and metric evaluation

- **Feature-correlation JSON is finite-only.**
  `ai/scripts/feature_correlation.py` treats `NaN` and both infinities as
  incomplete feature/target rows, rejects non-finite
  `--redundancy-threshold` values during argument parsing, and validates   complete report with `allow_nan=False` before its atomic manifest write.
  Preserve missing-scikit-learn empty-map contract and repeated
  constant-column check after complete-case filtering; neither `NaN` nor
  `Infinity` is RFC-8259 JSON value.
- [ADR-0963](../../docs/adr/0963-ai-nan-propagation-guards-round25.md) — **NaN propagation guards in `eval.correlations` and `tune._read_best_metric`.** `correlations()` raises `ValueError` on empty inputs, returns `plcc=0.0, srocc=0.0` (with `RuntimeWarning`) for constant-valued inputs. 0.0 sentinel is intentional: gate logic uses `>=` and NaN would silently fail every comparison. `_read_best_metric()` returns `float("inf")` when all metric rows are NaN (diverged training run), preventing Optuna study corruption. Do not change these sentinels without updating downstream `_gate` comparison semantics in `bisect_model_quality.py`.
