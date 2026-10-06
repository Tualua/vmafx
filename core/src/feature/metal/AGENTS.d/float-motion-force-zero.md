---
paths:
  - core/src/feature/metal/float_motion_metal.mm
  - core/src/feature/metal/float_motion.metal
invariant: float_motion force-zero ownership and flush idempotency (BUG048 A5).
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# float_motion force-zero ownership and flush idempotency (BUG048 A5)

- `init_fex_metal()` releases the device lifecycle before returning from the
  `motion_force_zero` path, but the cloned extractor still owns its
  `feature_name_dict`. Keep `close_fex_metal` (or an equivalent dictionary-owning
  callback) installed; restoring `fex->close = NULL` leaks the dictionary.
- Before appending the tail `VMAF_feature_motion2_score`, `flush_fex_metal()`
  resolves the actual score name through `feature_name_dict` and probes that
  name at `s->frame_index`. A literal-name probe misses option-derived names such as
  `motion_fps_weight=1.5` and makes a repeated flush fail.
- `collect_fex_metal()` gates emission of `VMAF_feature_motion_score` behind
  `if (s->debug)`.
- `test_metal_float_motion_parity` exercises both lifecycle and flush idempotency
  invariants on Apple Silicon, and skips cleanly on Linux/Windows.
- `test_metal_float_motion_contract.py` enforces struct fields, option
  registrations, close callback retention, debug gating, and dictionary-resolved
  flush idempotency at AST level. See [Research-2113](../../../../../docs/research/2113-metal-float-motion-lifecycle-flush.md).
