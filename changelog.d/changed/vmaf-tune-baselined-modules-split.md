- **`vmaf-tune`'s `auto`, `bisect`, `executor`, `per_shot`, `prefilter` and
  `score` modules meet the HISS-04 size limits and cite the right ADRs.** The 14
  functions over 60 lines (among them `auto.run_auto` at 327 lines and
  `bisect.bisect_target_vmaf` at 368) are split into helpers without a change in
  behaviour, and the HISS baseline loses those 14 infractions. Comments and
  docstrings now cite the current records (the conformal intervals are ADR-0393,
  Phase F `auto` is ADR-0397, the workdir is ADR-0598), the Pelorus records in
  `prefilter` are named as Pelorus's, and the `auto` docstrings count ten
  short-circuits. The JSON `notes` text of `prefilter --smoke` and the
  production result changed with the citations.
