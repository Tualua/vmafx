---
paths:
  - core/tools/cli_feature_backend.cpp
  - core/tools/cli_feature_backend.h
  - core/tools/vmaf.cpp
invariant: register_cli_feature() maps CPU extractor to twin through libvmaf; backend_used reflects device if any ran.
---
# `--feature` twin routing and backend receipt (ADR-1359)

- With explicit device `--backend`, `register_cli_feature()` maps CPU extractor
  name to backend twin through `cli_choose_feature_extractor()`
  (`cli_feature_backend.cpp`), which asks libvmaf's
  `vmaf_feature_backend_twin()`. Pairing, option gate and geometry check live
  in libvmaf, shared with model dispatch. Never add CLI-side name table or
  `<name>_<backend>` mangling (HISS-19): twin names follow no pattern
  (`ssim` -> `integer_ssim_cuda`, `ciede` -> `integer_ciede_metal`).
- Order in `register_cli_feature()` load-bearing: ADR-0543 suffix gate
  first (twin names keep exit 100), then twin choice. Warning printed
  before `vmaf_use_feature()` consumes options: warning text quotes
  option key owned by that dictionary.
- `cli_backend_is_device()` = single `auto` / `cpu` test;
  `explicit_backend_requested()` delegates there. `--backend cpu|auto` and
  unset `--backend` never call twin lookup.
- `-EINVAL` from lookup (twin name, unknown name, unparsable value) stays
  silent: registering given name reports real error.
- JSON receipt: `backend_used` derives from
  `vmaf_registered_feature_extractor()` after final flush (device if any
  extractor ran there, else `cpu`), plus `feature_backends` array. Value set
  of `backend_used` = output contract (smoke probes, RC1 tester, MCP servers
  compare against backend names): add fields, never new values.
- `test_cli_feature_backend` replaces libvmaf with fakes of both entry points
  (link seam, like `test_vmaf_close_retry`); keep `cli_feature_backend.cpp`
  free of other libvmaf calls, else test stops linking.
