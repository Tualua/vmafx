- **`vmaf-dev-llm[modelcard]` extra** (ADR-1528). It installs `onnx`,
  `onnxruntime`, `pandas`, `pyarrow` and `scipy`, which `vmaf-dev-llm modelcard`
  needs to read the ONNX graph and to score the model with `--features`.
  Without them the card leaves those facts out. See
  [the dev-llm README](dev-llm/README.md#install).
