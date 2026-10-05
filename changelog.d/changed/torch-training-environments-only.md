- torch is installed only by the two training packages, `ai/` and
  `tools/ensemble-training-kit/` (ADR-1886). The vmaf-tune predictor trainer
  moved to `vmaf_train.predictor_train` (`python -m vmaf_train.predictor_train`
  in the ai/ environment), and vmaf-tune's `train` extra is gone; vmaf-tune
  still loads the trained ONNX predictors. `describe_worst_frames` in the
  Python MCP server describes frames with a local vision-language model through
  ONNX Runtime GenAI: install `vmaf-mcp[vlm]` (now `onnxruntime-genai`, no
  torch or transformers) and point `VMAF_MCP_VLM_MODEL` at a model directory
  such as the CPU build of Phi-3.5-vision-instruct-onnx. The server no longer
  downloads models or runs model-hub code; without a model it returns frame
  metadata with a note that names what is missing.
