- **The Python extension builds again on every compiler.**
  `compat/python-vmaf/core/adm_dwt2_cy.pyx` re-declares `adm.c`'s static
  `init_dwt_band_d()` for Cython. PR #1859 changed that helper's cursor from
  `char *` to `double *` and its length from bytes to samples, but the
  declaration kept `char *`, so the generated `adm_dwt2_cy.c` passed a `char *`
  where a `double *` is expected and no C compiler accepted it (clang 22 on
  Ubuntu x86-64 and ARM, Homebrew clang on macOS, GCC too): the wheel build of
  the Python harness failed. The declaration, the cursor and the call now use
  `double *` and a length in samples; the contract test
  `test_cython_adm_dwt_band_decl_contract` keeps them in step with `adm.c`.
