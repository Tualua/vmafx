- **`test_icx_system_libm` passes on Windows.** Its glibc-probe cases patched
  `os.confstr`, which Windows' `os` module does not have, and the patch raised
  before the case ran (4 errors on the UCRT64 leg). The patches may now create
  the attribute, and a new case runs them all without it.
