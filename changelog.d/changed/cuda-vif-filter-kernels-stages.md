- **The CUDA integer VIF filter kernels are assembled from short stages
  (ADR-1142).** The four kernel bodies of
  `core/src/feature/cuda/integer_vif/filter1d.cu` were 133 to 225 lines each,
  the last functions of the GPU feature code above the 60-line limit. They
  now call inlined stages (tile load, taps, rounding, write-back), and the
  8-bit and 16-bit horizontal kernels are one template. The debt baseline
  drops from 260 to 256 recorded infractions. No behaviour change: `vif` and
  every other CUDA twin return the same values as before on an RTX 4090.
