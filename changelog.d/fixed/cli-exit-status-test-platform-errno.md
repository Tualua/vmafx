- **`test_cli_exit_status` passes on Windows and macOS.** It expected Linux's
  `ENOSYS` (38) in one case; the expected status is now computed from the
  platform's value (`256 - ENOSYS`), so the Windows legs no longer fail on a
  correct exit status.
