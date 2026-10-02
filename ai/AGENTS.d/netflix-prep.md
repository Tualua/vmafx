---
paths:
  - ai/data/netflix_loader.py
  - ai/data/feature_extractor.py
  - ai/data/scores.py
  - ai/train/dataset.py
  - ai/train/eval.py
  - ai/train/train.py
  - ai/tests/conftest.py
invariant: Netflix prep stack: requires_pytorch_lightning test guard; iter_pairs ladder regex; zero payload smoke.
---
<!-- markdownlint-disable MD013 MD060 -->
# Netflix-corpus training prep (ADR-0242 / ADR-0203)

Top-level [`ai/data/`](../data/) and [`ai/train/`](../train/) packages
(distinct from `vmaf_train` package under `src/`) host runnable
Netflix-corpus prep stack:

- [`ai/data/netflix_loader.py`](../data/netflix_loader.py) — pair distorted
  YUVs with their ref by parsing Netflix ladder filename
  convention. `iter_pairs(data_root, *, sources=, max_pairs=,
  assume_dims=)` = only public surface.
- [`ai/data/feature_extractor.py`](../data/feature_extractor.py) — wraps
  libvmaf CLI in JSON mode. Defaults to
  `core/build-cpu/tools/vmaf`; honours `$VMAF_BIN`. Raises
  `RuntimeError` with explicit build instructions on missing binary.
- [`ai/data/scores.py`](../data/scores.py) — `vmaf_v0.6.1` distillation
  scores (per-frame + pooled). Honours `$VMAF_MODEL_PATH`.
- [`ai/train/dataset.py`](../train/dataset.py) — `NetflixFrameDataset`
  with explicit `payload_provider=` + `assume_dims=` injection points
  for unit tests.
- [`ai/train/eval.py`](../train/eval.py) — PLCC / SROCC / KROCC / RMSE +
  latency. Either `onnx_path=` or `predictions=` (exactly one).
- [`ai/train/train.py`](../train/train.py) — CLI entry point. Runs
  standalone (`python ai/train/train.py …`) or as module
  (`python -m ai.train.train`); both forms work because script
  fixes `sys.path` when `__package__` is empty.

**Rebase-sensitive invariants** (track when upstream Netflix/vmaf adds
its own training surface):

- **`ai/tests/conftest.py::requires_pytorch_lightning()` = canonical
  guard for tests importing `vmaf_train.models` (transitively
  `pytorch_lightning` → `torchmetrics` → `torchvision`).** Plain
  `pytest.importorskip("pytorch_lightning")` NOT sufficient — it
  only catches `ImportError`, while torchvision/torch ABI mismatches
  raise `RuntimeError("operator torchvision::nms does not exist")` at
  module load. New tests pulling lightning must call
  `requires_pytorch_lightning()` at module level (or use
  `_PYTORCH_LIGHTNING_ERROR` constant with `pytest.mark.skipif` for
  per-function gating). Guard is intentionally broad
  (`except Exception`) so future torch/torchvision/torchmetrics drift
  also routes to clean skip with actual error string. Behavior
  contract pinned by `ai/tests/test_conftest_pytorch_lightning_guard.py`.
- `iter_pairs` filename regex = fork-specific. If upstream adds
  loader with different ladder convention, do NOT merge them — keep
  ours under `ai/data/` and theirs under whatever path they pick.
- Per-clip JSON cache schema (`{features:{feature_names,
  per_frame, n_frames}, scores:{per_frame, pooled}}`) consumed by
  both dataset and any downstream consumer. Bumping schema
  must invalidate `$VMAF_TINY_AI_CACHE` (or version-tag path).
- Smoke command `python ai/train/train.py --epochs 0
  --assume-dims 16x16` MUST stay runnable without a built `vmaf`
  binary — `_make_zero_payload` helper in `ai.train.dataset`
  injects fake payload so CI gates don't drag libvmaf build into
  Python test surface.
