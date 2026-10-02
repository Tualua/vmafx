- **Seven SYCL runtime files conform to clang-tidy and HISS standards (batch B6).**
  The SYCL runtime source files and headers (`core/src/sycl/common.cpp`,
  `core/src/sycl/d3d11_import.cpp`, `core/src/sycl/dmabuf_import.cpp`,
  `core/src/sycl/common.h`, `core/src/sycl/dmabuf_import.h`,
  `core/src/sycl/picture_sycl.h`, `core/src/sycl/picture_sycl.cpp`) were brought to zero non-host-FP clang-tidy
  debt and full HISS compliance under [ADR-1142](docs/adr/1142-whole-codebase-standards.md).
  Eliminated 6 HISS infractions (3 gotos, 3 oversized functions). Numerical
  correctness is bit-identical on Intel Arc A380 at `--precision max` on the
  Netflix 576x324 reference pair, and the SYCL scratch memory audit passes (zero scratch memory used).
