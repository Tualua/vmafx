- Rewrote the backend guide around a newcomer's choice: `backends/index.md`
  opens with a table of hardware, backend, build option, `--backend` value, SDK
  and exactness status, then explains selection and numerical agreement. The
  SYCL, HIP and CUDA overviews (13,000, 10,000 and 7,400 words) are now short
  overviews with twin tables and open gaps only; per-twin notes, AOT and
  zero-copy details and the dated history moved to their own pages. Corrected:
  `VMAF_CUDA_DISPATCH` is `direct|graph`, HIP registers 19 extractors (the
  `-ENOSYS` stubs are gone), Metal 17, `ciede_cuda` exists, and
  `--backend sycl --feature cambi` runs the SYCL twin (ADR-1359).
