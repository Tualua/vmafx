---
paths:
  - core/tools/spinner.h
  - core/tools/vmaf.cpp
invariant: spinner resolves glyphs from console capability; Windows targets enter through wmain, converting CP_UTF8.
---
# Progress-line rendering is console-capability-driven (ADR-1166)

`spinner.h` now carries two glyph tables and two selectors, and `vmaf.cpp`
resolves them from console's **actual** capabilities:

- `spinner[]` — upstream UTF-8 braille table. Byte-for-byte unchanged;
  `core/test/test_spinner.cpp` pins first and last entries, asserts
  every entry is exactly 6 bytes, so well-meaning re-encode (universal
  character names, different braille range) fails fast suite. Never
  rewrite these literals as `\uXXXX` escapes: MSVC's narrow execution charset
  is ANSI code page, where they would not round-trip.
- `spinner_ascii[]` + `spinner_table_for_codepage()` + `spinner_erase_eol()` —
  fallback for console reporting non-UTF-8 code page or refusing VT
  processing.

On POSIX both selectors called with `SPINNER_CODEPAGE_UTF8` and
`vt_enabled = 1`, so emitted bytes are identical to pre-ADR-1166 form.
Keep it that way — golden-gate CLI invocations parse this stream.

`WindowsConsoleGuard` in `vmaf.cpp` has static storage and is initialised
before `cli_parse()`. That is load-bearing: `cli_parse()` calls `exit()` for
help, version and parse errors, which skips automatic destructors but runs
static destructors. `CliRunGuard` is created immediately after successful
parsing and owns all ordinary-return cleanup.

## Windows CLI arguments are strict UTF-8 (ADR-1182 follow-up)

Windows `vmaf` and `vmafx` targets enter through `wmain`, convert every
UTF-16 token with `WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ...)`,
and only then call parser shared with POSIX `main`. Keep conversion
before `cli_parse()`: reference, distorted, output, and model paths must reach
existing UTF-8 path layer before any option handler can copy them. Invalid
UTF-16 must fail closed, not use replacement characters.

GNU-style Windows linkers need `-municode` on both CLI targets so CRT startup
selects `wmain`; MSVC-style linkers infer entry point. Do not apply that
flag to unrelated tools with narrow `main`. Windows-only
`test_vmaf_windows_utf8_argv` regression launches built binary through
`CreateProcessW` and checks exact accented+CJK output path. POSIX entry and
argument bytes remain unchanged.
