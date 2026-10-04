- **No `ai/scripts` file announces itself as "not yet implemented".** Ten files
  printed that text and exited 1. `gen_dists_sq_placeholder_onnx.py` and
  `gen_mobilesal_placeholder_onnx.py` are implemented: they rebuild the shipped
  `model/tiny/dists_sq.onnx` and `model/tiny/mobilesal.onnx` byte for byte, and
  `--check` fails when a file differs. The other eight stubs (a LOSO evaluator,
  the pVMAF benchmark, the LSVQ fetcher, two corpus converters, two trainers and
  a stub named after the real `scripts/gen_ssimulacra2_eotf_lut.py`) are removed.
  The two model cards describe what the generators write: the ONNX file only.
