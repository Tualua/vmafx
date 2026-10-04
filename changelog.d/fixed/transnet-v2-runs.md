- **`--feature transnet_v2` opens the shipped model and detects cuts.** The
  extractor failed to open `model/tiny/transnet_v2.onnx` (`-34`: its rank-5
  input exceeded the session's shape probe, and the output it bound had another
  name). Two more faults hid behind that: thumbnails were scaled to 0..1 where
  the network expects 0..255, and each frame's probability was read from the
  window's last slot, which has no later frame to compare with; either alone
  kept a hard cut below 0.1. The extractor now runs upstream TransNet V2's
  `predict_frames()` windows (100 frames, stepping by 50, the middle 50
  reported, padding at both ends), so a frame's `shot_boundary_probability` and
  `shot_boundary` appear up to 74 frames after it is read and the last ones at
  flush; a `1.0` marks the last frame of a shot
  ([ADR-1527](docs/adr/1527-transnet-v2-upstream-windows.md)). Frames must
  arrive in order without index gaps.
