- **Ten fork-authored files carry the licence ADR-1250 gives them.** They were
  added in September 2026 with `BSD-2-Clause-Patent` or `BSD-3-Clause-Clear`
  tags, and `scripts/dev/relicense_fork_files.py` classifies them as fork work
  with no veto: no upstream path or name, no notice but the fork's, no outside
  author. They are `EUPL-1.2` now: `core/tools/vmaf_close_retry.c` and `.h`,
  `core/tools/test/test_vmaf_close_retry.c`,
  `core/tools/test/test_vmaf_read_error_exit.sh`,
  `core/test/test_gpu_option_alias_contract.py`,
  `core/test/test_predict_nonfinite_log_output.py`,
  `core/test/test_predict_source_authority.py`,
  `python/test/golden_gate_isolation_test.py`,
  `scripts/ci/setup-golden-build.sh` and
  `scripts/ci/tests/test_golden_gate_makefile_contract.py`. Only the tag line
  changes. The tool's other 31 pending entries are not applied: they are listed
  with what each needs in `T-RELICENSE-CHECK-PENDING-2026-10-02`
  ([ADR-1250](docs/adr/1250-eupl-fork-relicense.md)).
