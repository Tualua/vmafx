---
paths:
  - ai/scripts/collect_gpu_calibration_data.py
  - ai/scripts/calibrate_nr_threshold.py
invariant: GPU calibration names registered backends only; Fast-NR calibration sidecars require passing sample and PLCC gates.
---
<!-- markdownlint-disable MD013 MD060 -->
# GPU calibration data and Fast-NR quality guards

- **GPU calibration fixtures name live backends only.** Vulkan was removed by
  ADR-0726. Keep `collect_gpu_calibration_data.py` help, its manifest fixtures,
  and backend selections on registered CUDA/SYCL set; do not revive
  `vulkan_device`, `vulkan:lavapipe`, or `backends=["vulkan"]` as harmless test
  data because those examples are copied into real calibration manifests.
  Its score loader rejects top-level non-object, missing/non-list `frames`
  member, and non-object frame entries before metric pairing.
- [ADR-0665](../../docs/adr/0665-fast-nr-calibration-quality-guard.md) — **Fast-NR calibration sidecars are quality-gated.** `ai/scripts/calibrate_nr_threshold.py` must not write tune-facing `calibration_threshold` values unless fitted sample count and NR-vs-FR PLCC pass script's gates. Operator explicitly uses `--allow-weak-calibration` for diagnostic sidecars otherwise. Weak real-corpus fits = model/training backlog, not reason to loosen `vmaf-tune` thresholds.
