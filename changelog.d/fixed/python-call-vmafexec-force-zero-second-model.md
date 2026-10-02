- **The Python harness accepts `motion_force_zero` with more than one model.**
  `ExternalProgramCaller.call_vmafexec()` raised `AssertionError` for the
  second model when `motion_force_zero=True`: the loop over the models
  replaced the argument with the string `"true"` and then failed its own type
  check (the same statement is in Netflix upstream). Every model now gets the
  overload. A run with one model, including every golden test, produces the
  same command as before
  (`T-PYTHON-CALL-VMAFEXEC-FORCE-ZERO-SECOND-MODEL-2026-10-02`).
