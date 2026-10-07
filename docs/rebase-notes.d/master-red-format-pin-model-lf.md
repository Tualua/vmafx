## Model JSON checked out with LF, generator format test pinned to the hook's clang-format (2026-10-08)

`fix/master-red-format-pin-model-lf`, no ADR (bug fixes). `.gitattributes` adds
`model/**/*.json text eol=lf` after upstream's `*.pkl` / `*.model` lines: an
upstream sync that touches `.gitattributes` keeps the fork's line, or Windows
builds report other model hashes again (`test_praetor_hashed_files_lf.py`
fails). `scripts/codegen/tests/support.py::pinned_clang_format()` ties the
format test to the `clang-format` hook's major in `.pre-commit-config.yaml`;
bump the hook rev and `requirements/locks/tooling-tests.in` together.
`core/test/meson.build` gives `test_gpu_picture_pool_uaf` `MALLOC_PERTURB_=0`.
No upstream file besides `.gitattributes`.
