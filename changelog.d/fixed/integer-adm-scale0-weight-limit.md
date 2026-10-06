- **Integer ADM halves a scale-0 CSF weight from 43900 instead of 46603.**
  The CSF stage keeps the 1/30 magnitude of the weighted band in 16 bits,
  which wrapped negative (scalar code, AVX-512, CUDA, HIP, SYCL, Metal) or
  saturated (AVX2) for horizontal or vertical weights between 43900 and the
  old limit, and a masking row of a 31-32 pixel wide picture could pass 2^64
  from about 45200. Such a weight now takes one more halving on every backend
  ([ADR-1917](docs/adr/1917-integer-adm-scale0-weight-limit-csf-magnitude.md)).
  Watson97, the default Barten configuration and both blend modes are
  unchanged; a Barten configuration in that range (for example
  `adm_csf_scale=1.16:adm_csf_diag_scale=0.3`) moves by about 1e-6.
