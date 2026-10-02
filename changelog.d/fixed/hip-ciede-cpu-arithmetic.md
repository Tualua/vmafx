- **`ciede_hip` follows the CPU extractor's arithmetic** (ADR-1448). The HIP
  twin computed CIEDE2000 in single precision with another form of the
  formula and added per block: it matched `ciede` on no frame and was up to
  1.1e-5 from it. It now runs the CPU's statements on pairs of `float`
  values, from the headers the SYCL twin uses
  (`core/src/feature/ciede_ff_math.h`, `core/src/feature/ff_math.h`, moved
  out of `core/src/feature/sycl/` unchanged), and the host adds the
  per-pixel values in the CPU's order. Measured on a gfx1036 at
  `--precision max`: 115 of 178 frames identical and the rest within
  1.4e-11. What remains is glibc's `powf`, which is not correctly rounded
  (2 206 of 437 million pixels), and the last bits of a pair (8 pixels); the
  parity gate bounds the cell at `1e-9`. Stored `ciede_hip` scores change by
  up to 1.1e-5. A frame takes 2.7 times as long (49.6 ms at 1920x1080, 210 ms
  at 3840x2160). `ciede_sycl` returns the same bits as before on an Arc A380.
