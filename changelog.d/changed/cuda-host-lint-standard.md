- **The CUDA runtime and host files are clean under clang-tidy (ADR-1142).**
  `core/src/cuda/picture_cuda.c` initialises its copy descriptors with their
  memory types instead of a zero that is no enumerator; `picture_cuda.h` and
  `cuda_helper.cuh` get include guards that are not reserved identifiers;
  `integer_psnr_hvs_cuda.c` loads its kernel module in a helper, converts its
  kernel arguments explicitly and bounds its plane loop by the size of the
  header's offset table (14 findings the baseline did not record);
  `integer_cambi_cuda.c`, `common.h` and `cuda_helper.cuh` carry the cited
  suppressions for constructs C requires. Every file under `core/src/cuda/`
  and `core/src/feature/cuda/` measures 0 in the `cuda` lane; its baseline
  drops from 734 to 728. No behaviour change: every CUDA twin returns the
  same values as before on an RTX 4090 (18 192 of 18 192 values of the
  sweep).
