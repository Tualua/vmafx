- **Eleven licence tags now say what the file's own notice says.** The SPDX
  backfill had given some files the identifier of their directory rather than
  of the notice in them. Files that carry only Daala's, Xiph.Org's, dav1d's,
  LIME's or Danny Yoo's two-condition BSD text are `BSD-2-Clause`
  (`core/tools/vidinput.c`, `core/tools/y4m_input.c`,
  `core/src/feature/third_party/xiph/psnr_hvs.c`,
  `core/src/feature/integer_ssim.h`, `core/src/compat/gcc/stdatomic.h`,
  `compat/python-vmaf/core/local_explainer.py`,
  `compat/python-vmaf/tools/scanf.py`); files with Netflix's header and a quoted
  third-party notice name both licences (`core/src/svm.cpp`:
  `BSD-2-Clause-Patent AND BSD-3-Clause`; `core/src/feature/ciede.c`:
  `BSD-2-Clause-Patent AND MIT`; `compat/python-vmaf/tools/sigproc.py`:
  `BSD-2-Clause-Patent AND BSD-2-Clause`); `core/src/feature/iqa/ssim_simd.h`,
  which has Netflix's header, is `BSD-2-Clause-Patent`. No notice text changed
  and no file changed licence: the tags were wrong, not the terms. A pre-commit
  check compares tag and notice from now on
  (`T-SPDX-TAG-DISAGREES-WITH-NOTICE-2026-10-02`,
  [ADR-1250](docs/adr/1250-eupl-fork-relicense.md)).
