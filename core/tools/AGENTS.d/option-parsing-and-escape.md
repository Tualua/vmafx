---
paths:
  - core/tools/cli_parse.cpp
  - core/tools/cli_parse.h
invariant: Option dicts belong to CLISettings; parse_unsigned rejects negatives; long-only options pass enum to error().
---
# Option parsing, string escaping, and unsigned bounds

- **Option dictionaries belong to `CLISettings` until libvmaf takes
  them.** `cli_free()` releases every `feature_cfg[i].opts_dict` and
  `model_config[i].feature_overload[j].opts_dict` still set.
  Early abort (unopenable input, bad geometry, mismatching model/feature,
  unknown extractor) leaks nothing.
  [vmaf.cpp](../vmaf.cpp) — `vmaf_use_feature()`, `vmaf_model_feature_overload()`,
  `vmaf_model_collection_feature_overload()` — clears settings' pointer
  with `std::exchange` first; new call site must too, or run
  double-frees. `use_cli_feature()` restores options only for unknown
  extractor name, one `-EINVAL` on which libvmaf hands them back; it tells
  that case apart with second `vmaf_use_feature()` call without options,
  which for known name registers default-options instance. That is sound
  only while registration failure ends run (context, instance
  included, is closed right after); change that lets run continue past
  failed `--feature` must replace probe.
  Regression tests: `test_vmaf_option_dict_ownership` (run it in ASan
  build) and `test_cli_parse` (`release_parsed()`).
- **Long-only options must not pass synthesised short-option
  chars to `error()`** (rebase-sensitive). Handlers for
  `ARG_THREADS`, `ARG_SUBSAMPLE`, `ARG_CPUMASK`, and any
  future `ARG_*` enum value `>= 256` MUST pass that enum value
  (not fabricated `'t'` / `'s'` / `'c'`) into
  `parse_unsigned()` / `parse_bitdepth()` / `error()`.
  `error()` table-walk over `long_opts[]` for non-existent
  short-option char trips `assert(long_opts[n].name)`,
  takes binary down with `SIGABRT`.
  `error()` `< 256` branch already handles long-only options
  via `--name` path; passing real enum value is
  required to reach it. See
  [ADR-0316](../../../docs/adr/0316-cli-parse-long-only-error-fix.md);
  parked-then-promoted reproducer
  `core/test/fuzz/cli_parse_corpus/cli_threads_abbrev_assert.argv`
  protects rebase, and
  `core/test/test_cli_parse_long_only_args.c` protects
  unit-test path.
- **`cli_parse.cpp::usage()` discrete overloads** (rebase-sensitive).
  `usage()` provides discrete template overloads for 1, 2, and 3 arguments
  and no variadic parameter-pack fallback. This prevents zero-argument pack
  expansions that trip CodeQL
  `cpp/unused-local-variable` and `cpp/unused-static-variable` (Alerts 1002/1003).
  Do not collapse back into unconstrained variadic pack without verifying
  CodeQL analysis. Adversarial regression coverage is pinned by
  `core/test/test_cli_parse_long_only_args.c`.

- [ADR-1190](../../../docs/adr/1190-cli-option-string-escape-grammar.md) —
  **Escape-aware `--model` / `--feature` option-string splitting.**
  `cli_parse.cpp` no longer contains `strsep` (nor `vmaf_cli_strsep`
  shim or its `#ifndef HAVE_STRSEP` fork); nine split sites all go
  through `cli_split()` plus `cli_unescape_key()` /
  `cli_unescape_value()` (ADR-1355).
  **Rebase invariants**:
  - Splitting and unescaping are two passes. `cli_split()` must leave
    backslash sequences intact — escape written for `:` pass stays
    literal at `=` pass. Leaf unescaper runs exactly once per token,
    after last split that token undergoes.
    Unescaping earlier eats user's literal backslash; unescaping twice
    eats it again.
  - Keys and values unescape differently (ADR-1355). Keys, `--feature`
    name, both overload-key halves: `cli_unescape_key()` (`\:` `\=` `\.`
    `\\`). Values (paths, names, option values): `cli_unescape_value()`
    — backslash = data unless in run directly before `:` / `=` or at
    value end; such run reads in pairs, leftover lone backslash escapes
    `:` / `=`. Pairing matches `cli_split()` (`:` after odd run =
    literal). Never route value through key unescaper: `..\`,
    `\\server`, `\.cache` lose bytes.
  - Key/value pair's value is whole remainder after first
    unescaped `=` — never second split. Removed second split =
    silent-truncation bug (`path=/a/dir=eq/m.json` became `/a/dir`), so if
    `/sync-upstream` restores `strsep(&key_val, "=")` pair, drop it.
  - `apply_model_opt()` splits overload key on `.` **before**
    unescaping it, compares *raw* key against `path` / `name` /
    `version` / `disable_clip` / `enable_transform` (none of which contain
    escapable byte, so comparison is unambiguous).
  - `cli_is_drive_colon()` is ergonomics affordance, not
    optimisation: dropping it makes every Windows `path=C:\...` require
    `C\:`, which is user-visible complaint Netflix/vmaf#766 filed.
  - Go escaper `pkg/cliopt.EscapeValue` = same grammar in another
    language; change both (and its round-trip test) together.

## `parse_unsigned` rejects negatives on purpose (ADR-1209)

`parse_unsigned` refuses leading `'-'` before calling `strtoul`, because
POSIX `strtoul` silently converts `"-1"` to `ULONG_MAX` without setting
`errno`. Upstream relies on that wraparound — its own
`test_vmaf_cuda_gpumask.sh` passes `--gpumask -1`, expects it to mean "all
bits set". Never loosen check to make inherited script pass; fix
caller instead. `--gpumask 1` means same thing, says so.

More generally, `--gpumask` is not per-op bitmask despite `$bitmask`
placeholder: passing flag opts into GPU backend selection, any non-zero
value then disables GPU feature extractors, so run falls back to CPU.
`--gpumask 0` = use GPU, `--gpumask 1` = use CPU.
