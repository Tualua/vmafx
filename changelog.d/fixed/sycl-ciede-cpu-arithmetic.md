- **`ciede_sycl` follows the CPU `ciede` to 1.4e-11.** The CPU extractor
  computes CIEDE2000 in `double` and stores in `float`. The SYCL twin
  computed in `float` throughout, used the device's `float` math functions
  and a rewritten form of one branch, and was up to 1.14e-5 from the CPU. A
  SYCL kernel has no `double`, so the kernel now runs the CPU's statements
  with every `double` as a pair of `float` values and every math-library
  call as a function on such pairs, and the host adds the per-pixel values
  in the CPU's order
  ([ADR-1436](docs/adr/1436-sycl-ciede-cpu-arithmetic.md)). Measured on an
  Arc A380 at `--precision max`: the Netflix 576x324 pair identical to
  `--backend cpu` on 47 of 48 frames, its 10-, 12- and 16-bit versions and
  both 1080p checkerboard pairs on every frame, 200 frames of BBB 3840x2160
  within 1.4e-11. These are the CUDA twin's figures. It is not
  bit-identical: the C library's `powf` is not correctly rounded and 18 to
  64 of the 8.3 million pixels of a 3840x2160 frame round the other way. The parity
  gate compares the twin at `1e-9` instead of `5e-3`. A 3840x2160 frame
  takes 50.3 ms on an Arc A380 instead of 16.2 ms, and the twin holds 33 MB
  more on the device and on the host at that size. Stored `ciede_sycl`
  scores change by up to 1.14e-5.
