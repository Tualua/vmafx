- **`float_adm` refuses frames smaller than 17x17 instead of reading outside
  its buffers.** The float ADM extractor decomposes each frame into four
  wavelet levels. Below 17 pixels in width or height the coarsest level has a
  single sample, and the extractor read next to it: at 8 pixels or fewer the
  read lands before the start of a heap buffer (confirmed with
  AddressSanitizer), from 9 to 16 it picks up a sample of another level, so
  the scores were not meaningful (a random 8x8 pair scored `adm_scale3 =
  1.05`). `float_adm` and `float_adm_cuda` now fail at start with
  `float_adm requires width >= 17 and height >= 17 (got WxH)`, as the
  fixed-point `adm` extractor already does. Frames of 17x17 and larger are
  unaffected. The SYCL, HIP and Metal `float_adm` twins still accept smaller
  frames.
