<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1461: no C or C++ translation unit is built with floating-point contraction; the strict policy is a project argument

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `build`, `numerics`, `aarch64`, `clang`, `gcc`, `testing`, `golden-gate`, `rc3`, `fork-local`

## Context

A compiler may turn `a * b + c` into one fused multiply-add, which rounds once
where the source rounds twice. clang does it by default within an expression
(`-ffp-contract=on`), GCC does it for C++ (`fast`) and not for C under
`-std=c23`, and neither can on x86-64 without `-mfma`, because the baseline
has no such instruction. On aarch64 the instruction is baseline.

`core/src/meson.build` turned contraction off one library at a time
(`vmaf_strict_fp_args`): 20 libraries by ADR-0873, ADR-1057, ADR-1207 and
ADR-1415, each added when a test or a lane failed. The feature library, the
SVM, the model code, the tools and the tests were not among them. Measured
with cross builds under `qemu-aarch64` 11.1.1, the scores on master
`a30e73033` and the instruction counts on that day's earlier master
(`T-AARCH64-CLANG-FP-CONTRACT-FEATURE-LIB-2026-10-02`):

- the clang 22.1.8 build held 1127 fused instructions in 29 files of the
  feature library, 132 in `svm.cpp`, 12 in `predict.c`, 120 in the library's
  own objects; the GCC 16.1 build held 119 in `svm.cpp` and 19 in two C++
  files of the feature library;
- 17 extractors and the default model at `--precision max` on five fixtures
  (the Netflix 576x324 pair at 8 and 10 bits, both 1080p checkerboard pairs,
  4 frames of BBB 3840x2160; 3355 values): the clang build and the GCC build
  agreed on 2675 values and differed on 680: `speed_chroma` up to 6.2e-5,
  `speed_temporal` up to 2.3e-2 on a checkerboard, `float_vif` scales up to
  3.5e-5, `adm_scale1` 2.4e-6, `motion` 9.5e-7, the model score 1.6e-12;
- the GCC aarch64 build differed from the GCC x86-64 build on 23 of the 3355
  values (the model score through `svm.cpp`, and `psnr_hvs`), the clang
  aarch64 build on 694.

The same code, the same input and three different sets of scores, depending
on compiler and architecture.

## Decision

The strict policy (`vmaf_strict_fp_args`) is a Meson project argument for C
and C++, declared in `core/src/meson.build` above the first build target.
Every translation unit the C and C++ compilers build takes it: every library,
the tools and the tests. The Obj-C++ Metal wrappers take it through
`metal_objcpp_args`. A fused multiply-add the arithmetic wants stays what it
was: spelled in the source as an intrinsic or `fma()` / `fmaf()`.

A target's own arguments follow the project arguments, so no target may name
anything that turns contraction back on; under icx that includes
`vmaf_fp_model_args` alone (`-fp-model=precise` implies `-ffp-contract=on`).
The five targets that did (the feature library and four test executables)
now name nothing.

`core/test/test_strict_fp_compiler_args.py` pins it three ways: the project
argument exists once, after the policy block and above the first target; no
`meson.build` under `core/` names a contraction-enabling flag outside the
policy block; and, when run by `meson test`, every C and C++ command in the
build's own compile database ends its floating-point flags on the strict one.

`make test-netflix-golden-arm64` runs the Netflix golden gate against an
aarch64 cross build (GCC or clang) under qemu-user, so the property is
checked on the architecture where it failed.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
| --- | --- | --- | --- |
| Add `vmaf_strict_fp_args` to each library that contracted (feature library, SVM, `predict_c`, the library target, the tools) and to the test executables | The mechanism already in use | It is how the gap arose: a target that forgets the list is silently contracted, and the list has to be repeated on every new target | Not robust |
| Append the policy to `vmaf_cflags_common` | One line; most targets take that list | The list is also pasted into the icpx command lines of the SYCL kernels, where a host compiler's MSVC-style spelling would be read as a file name; a target that does not use the list is missed | Wrong scope on both sides |
| Function-scoped guards (the ADR-1057 pattern) on the affected functions | Touches no build file | 1127 fused instructions in 29 files; every new expression needs a guard | Does not scale |
| Leave it and document that aarch64 clang builds differ | No change | Two `speed_chroma` outputs and `speed_temporal` differ by more than the 5e-5 the snapshot gate allows; a GPU twin declared exact against one build is not exact against the other | The scores are wrong on one of the builds |
| **Project argument (chosen)** | Every C and C++ target, present and future, whatever list it names; the device compilers keep their own policies; one line | Meson requires it above the first target, so the policy block moves to the top of the file; a target-level flag can still undo it, which the test checks | Chosen |

## Consequences

- **Positive**: on aarch64 a clang build and a GCC build return the same
  scores: 3350 of the 3355 values (2675 before). The five left are
  `ciede2000` values, up to 1.1e-12 apart, that differ between the compilers
  on x86-64 in the same way and are not a contraction
  (`T-CIEDE-CLANG-POWF-BUILTIN-2026-10-02`). The aarch64 GCC build equals the
  x86-64 GCC build on 3348 of 3355 values; the seven left are `psnr_hvs`
  values where the NEON kernel is not bit-identical to the scalar one
  (`T-PSNR-HVS-NEON-NOT-SCALAR-BITS-2026-10-02`, present before). Scalar
  references in the tests are built under the same policy as the library.
- **Negative**: scalar code on aarch64 and on x86 builds with `-march` set by
  the user no longer gets compiler-chosen fused multiply-adds. The cost is
  not measured: the aarch64 builds ran under emulation, where time means
  nothing. Scores of existing aarch64 clang builds (macOS, the
  `Ubuntu ARM clang` lane) move by the amounts above, and those of aarch64
  GCC builds by 1.2e-12 in the model score.
- **Neutral / follow-ups**:
  - x86-64 is unchanged: with GCC the library and the tool have the same
    machine code with and without the argument (same build directory,
    rebuilt; 42 of 183 compile commands carried the flag before, 183 after),
    and a GCC build and a clang build each return the same 3355 values before
    and after.
  - The Netflix golden assertions are untouched. The three expectations that
    carry a Darwin value are compared at `places=2` and lie 6e-4 to 8e-4 from
    the Linux values, so either value satisfies them on either platform.
  - The Metal change (`core/src/metal/meson.build`) is not built on this
    host.
  - The per-library `vmaf_strict_fp_args` stay: they cost nothing and say
    which kernels depend on the policy.

## References

- `req` (maintainer, via the coordinator's brief of 2026-10-02 for
  `T-AARCH64-CLANG-FP-CONTRACT-FEATURE-LIB-2026-10-02`): fix it now, reusing
  the mechanism of the strict-FP lists.
- [ADR-0873](0873-arm64-neon-bit-exactness-audit.md),
  [ADR-1057](1057-revert-float-adm-simd-dispatch-neon-fma.md),
  [ADR-1207](1207-feature-isa-invariance-gate.md),
  [ADR-1415](1415-x86-simd-libraries-strict-fp.md) (the per-library lists),
  [ADR-1367](1367-sycl-strict-fp-every-feature-tu.md),
  [ADR-1403](1403-cuda-strict-fp-every-kernel.md),
  [ADR-1407](1407-hip-strict-fp-every-kernel.md) (the device policies),
  [ADR-1317](1317-golden-gate-build-isolation.md) (the golden build profile).
