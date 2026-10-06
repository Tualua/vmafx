<!-- markdownlint-disable MD013 MD032 MD060 -->
# `scripts/ci/lint-stubs/` — lint-only stand-in headers (ADR-2062)

`mex.h` and `matrix.h` under `scripts/ci/lint-stubs/matlab/` declare only what the ten sources of
`compat/python-vmaf/matlab/` call. A source that needs another MATLAB function adds its declaration
there (from the documented signature, never MathWorks text); `test_gen_mex_compile_commands.py`
compiles every MEX source against the stubs and fails when one stops parsing. The generator runs in
the `cpu` lane (`TIDY_RATCHET_COMPDB_cpu`) and in the `Generate compile_commands.json` step of the
changed-files job; it exits 1 with no sources or no stub, so the lane cannot go clean by measuring
nothing. `mexErrMsgTxt` stays `_Noreturn`. Do not add an exception or an `exclude_untidyable()`
entry for these files, and do not put the stub directory on any meson include path.
