- **Several CUDA instances on one device no longer get wrong `vif` scores.**
  `integer_vif_cuda` cleared its accumulators on its private stream while the
  scale 0 kernels that add into them ran on the picture stream, with nothing
  ordering the two. One instance never lost the race; with four instances on
  one `CUcontext` a late clear erased the first adds, and every run returned
  wrong `vif` scales (and, through the model, wrong VMAF) for tens of frames.
  The clear now runs on the picture stream. Measured on an RTX 4090 with four
  instances on one context, 48 frames of the Netflix 576x324 pair: 28 to 85
  wrong frames of 192 per feature in every run before, none in 105 runs after
  (Netflix/vmaf#1305).
