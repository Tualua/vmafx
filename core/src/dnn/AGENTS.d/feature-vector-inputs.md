---
paths:
  - core/src/libvmaf.c
  - core/src/dnn/model_loader.c
  - core/test/dnn/test_vmaf_use_tiny_model.c
  - core/test/dnn/test_cli.sh
invariant: Feature-vector tiny models request their inputs, score at flush, and fail on a missing input, never reading 0.0.
---
<!-- markdownlint-disable MD013 -->
# Feature-vector tiny-model inputs (ADR-1520)

- **Attach registers the inputs.** `dnn_attach_feature_vector()` in
  `core/src/libvmaf.c` resolves every input slot (sidecar `feature_order` /
  `features`, canonical-6 only for a six-wide model without a list) to an
  extractor through `dnn_slot_extractor()` and registers it with
  `vmaf_use_feature()` and default options. A name no extractor writes is
  `-EINVAL`, a list of another length than the input is `-ENOTSUP`; all names
  are resolved before the first registration.
- **Scoring runs at flush.** `dnn_flush_feature_vector()` is called from
  `flush_context()` after every backend flush, because the motion extractor
  writes `motion2` only in its own `flush()` and GPU twins collect their last
  frame there. Do not move rank-2 scoring back into `vmaf_read_pictures()`:
  every frame would read a missing `motion2`. Rank-4 image models stay on the
  per-frame path.
- **A missing input fails.** `dnn_lookup_feature()` returns `-ENOENT`, never
  `0.0`. A frame with some but not all inputs fails the flush and names the
  features; a frame with none (an index the caller skipped) and a frame
  `n_subsample` drops are not scored. `dnn.next_index` keeps a retried flush
  from appending a frame twice.
- **The codec block is the caller's.** A second input is accepted only when
  the sidecar's `encoder_vocab` plus two equals its width
  (`dnn_check_codec_layout()`). The block starts zero and `codec_ready` stays
  false until `vmaf_dnn_set_codec_context()` succeeds; until then
  `vmaf_read_pictures()` returns `-EINVAL`. `vmaf_dnn_codec_block_fill()`
  finds `"unknown"` by name and returns `-ENOENT` for a vocabulary without
  it; never default to a fixed slot such as the last or third-from-last.
- `test_vmaf_use_tiny_model.c` (`test_feature_vector_*`,
  `test_codec_*`) and the codec cases of `test_cli.sh` guard all four.
