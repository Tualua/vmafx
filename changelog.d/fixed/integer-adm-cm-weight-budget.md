- **Integer `adm` in Barten mode (`adm_csf_mode=1`) no longer wraps on
  high-contrast pictures (ADR-1472).** The contrast-masking reduction squares
  each weighted wavelet coefficient and narrows the square to 32 bits; with
  the weight limit of 2^30 that ADR-1325 chose for scales 1 to 3 the square,
  and on some pictures the weighted coefficient itself, could leave 32 bits.
  `vmaf --feature adm=adm_csf_mode=1` failed every frame of the 10 px
  checkerboard (`aim_num=-nan`) and returned `integer_adm2` 0.587 instead of
  0.784 on the 1 px checkerboard without any message. The weight limits now
  follow from the largest coefficient the wavelet can produce at each scale
  (`adm_csf_fixed_limit()`), so no picture can wrap. Barten-mode scores that
  were already right move by at most 7.7e-7 on the Netflix pair; Watson97 and
  the two blend modes are bit-identical. The GPU twins take the weights from
  the same header and follow without a kernel change (CUDA, HIP and SYCL
  measured: every `adm` output equals the CPU's).
