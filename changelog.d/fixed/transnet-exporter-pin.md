- **The TransNet V2 exporter pins a commit that exists.** `ai/scripts/export_transnet_v2.py`, the
  `transnet_v2` sidecar, its registry `license_url` and the model page named upstream commit
  `77498b8e`, which returns 404. They now name `a0942ca347ee00aa455631147641954278b1d1a5`, the
  commit that added the weights; its Git LFS object ids are the exporter's two pinned SHA-256
  values. The shipped ONNX file and every score are unchanged.
