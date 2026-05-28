# libFuzzer harnesses for parser surfaces.
#
# Opt-in: only built when `-Dfuzz=true` is passed to `meson setup`. Requires
# clang as the compiler — gcc does not ship libFuzzer. Pairs with
# AddressSanitizer (`-Db_sanitize=address`) for heap-error coverage.
#
# See ADR-0270 (initial scaffold) and ADR-0311 (this expansion: yuv_input +
# cli_parse) plus docs/development/fuzzing.md for the design rationale and
# operator runbook.

if not get_option('fuzz')
    subdir_done()
endif

if cc.get_id() != 'clang'
    error('-Dfuzz=true requires clang (libFuzzer is a clang feature). ' +
          'Re-run meson setup with CC=clang.')
endif

# `-fsanitize=fuzzer` ships both libFuzzer's runtime (the `main`
# replacement that calls LLVMFuzzerTestOneInput) and the SanitizerCoverage
# instrumentation libFuzzer needs. The library under test is built with
# `-fsanitize=fuzzer-no-link` (instrumentation only, no `main`); we apply
# that via meson's `b_sanitize` only on the harness target's c_args /
# link_args so it doesn't infect the main libvmaf build.
fuzz_flags = ['-fsanitize=fuzzer']

# Tool-source set shared by every harness that drives the vidinput stack.
fuzz_vidinput_sources = [
    '../../tools/y4m_input.c',
    '../../tools/yuv_input.c',
    '../../tools/vidinput.c',
]

fuzz_y4m_input = executable('fuzz_y4m_input',
    ['fuzz_y4m_input.c'] + fuzz_vidinput_sources,
    include_directories : [libvmaf_inc, test_inc, include_directories('../../tools/')],
    c_args : fuzz_flags + compat_cflags,
    link_args : fuzz_flags,
)

# fuzz_yuv_input — wraps the headerless raw-YUV reader. Same vidinput
# source set; the harness fixes (width, height, pix_fmt, bitdepth) at
# compile time and lets the fuzzer vary only the frame body bytes.
fuzz_yuv_input = executable('fuzz_yuv_input',
    ['fuzz_yuv_input.c'] + fuzz_vidinput_sources,
    include_directories : [libvmaf_inc, test_inc, include_directories('../../tools/')],
    c_args : fuzz_flags + compat_cflags,
    link_args : fuzz_flags,
)

# fuzz_cli_parse — wraps the argv-tokenising CLI parser. Links against
# libvmaf for `vmaf_version()` and the feature-dictionary symbols. Uses
# `-Wl,--wrap=exit` so the harness can intercept `usage()`'s `exit(1)`
# via a `__wrap_exit` longjmp shim instead of terminating the fuzzer
# process. See fuzz_cli_parse.c for the longjmp / setjmp contract.
fuzz_cli_parse_link_target = (get_option('default_library') == 'both'
    ? libvmaf.get_shared_lib()
    : libvmaf)

fuzz_cli_parse = executable('fuzz_cli_parse',
    ['fuzz_cli_parse.c', '../../tools/cli_parse.c'],
    include_directories : [libvmaf_inc, test_inc, vmaf_include,
                           include_directories('../../tools/')],
    dependencies : [getopt_dependency],
    c_args : fuzz_flags + compat_cflags,
    link_args : fuzz_flags + ['-Wl,--wrap=exit'],
    link_with : fuzz_cli_parse_link_target,
)
