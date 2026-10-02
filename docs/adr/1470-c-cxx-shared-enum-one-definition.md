<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1470: An enum that C and C++ translation units both see has one size: no C++-only underlying type other than `int`'s width

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `abi`, `lint`, `testing`, `rc3`, `fork-local`

## Context

clang-tidy's `performance-enum-size` asks for the smallest underlying type an
enum's values fit (`enum E : uint8_t`). C++ can spell that. C cannot on the
fork's required toolchains: the C23 spelling is not available under MSVC's
`/std:clatest` ([ADR-1138](1138-c-translation-units-keep-null.md)). Several
lint cleanups therefore wrote two heads for one enum:

```c
#ifdef __cplusplus
enum VmafVifNameSet : unsigned char {
#else
typedef enum VmafVifNameSet {
#endif
```

A C translation unit then gives the enum `int`'s size and a C++ one the fixed
type's. As long as a value never crosses from one language to the other this
is invisible. When it does cross, the two sides read different bytes: the
same pattern on `VmafSyclDispatchStrategy` made a C caller read 32 bits of an
8-bit return value and broke `test_gpu_dispatch_runtime`, and on
`VmafSyclPoolMethod` it had `libvmaf.c` pass an `int` where `picture_sycl.cpp`
read a byte (both caught before they reached master, PR #1837).

Every enum under `core/` with a C++-only underlying type, on master
`dedae7035`:

| Enum | Header | C++ type | C size | Crosses the language boundary today |
|---|---|---|---|---|
| `VmafVifNameSet` | `core/src/feature/nonfinite_score.h` | `unsigned char` (1 byte) | 4 bytes | No: its one user, `vmaf_vif_emit_scores()`, is `static inline`, so each translation unit passes the value to its own copy. Not in a struct, not behind a pointer. |
| `VmafModelType` | `core/src/model.h` | `unsigned int` | 4 bytes, held by a `UINT_MAX` enumerator | Yes, in a struct through a pointer: `read_json_model.cpp` writes `VmafModel::type`, `model.c` and `predict.c` read it. |
| `VmafModelNormalizationType` | `core/src/model.h` | `unsigned int` | 4 bytes, `UINT_MAX` enumerator | Yes, the same way (`VmafModel::norm_type`). |
| `VmafPixelRange` | `core/src/feature/luminance_tools.h` | `unsigned int` | 4 bytes, `UINT_MAX` enumerator | Yes, by value: `cambi.c` passes it to `vmaf_luminance_init_luma_range()`, defined in `luminance_tools.cpp`. |

The three `unsigned int` enums are the same size in both languages and are
pinned to it: the `..._ABI_UINT_MAX = UINT_MAX` enumerator forces a C compiler
to a 4-byte type, and `core/test/test_flush_context_ordering.c` asserts the
size. `VmafVifNameSet` is the one whose sizes differ. It is harmless today
and one struct member or one non-inline function away from the failure above.

## Decision

A header that a C translation unit includes does not fix an enum's underlying
type to anything but `int`'s width.

- `VmafVifNameSet` gets one definition for both languages, the plain C form,
  with `performance-enum-size` and `modernize-use-using` suppressed and cited
  on it.
- The three ABI-pinned `unsigned int` enums stay: their C++ spelling states
  the size the C side already has, and their `UINT_MAX` enumerator holds the
  C side to it.
- `core/test/test_c_cxx_enum_definition_contract.py` (suite `fast`, no
  compiler) walks the quoted includes from every `.c` file under `core/`,
  and in each header it reaches rejects an enum whose fixed underlying type
  is narrower or wider than `int`, and one that is `unsigned int` without a
  `UINT_MAX` enumerator. A header only C++ includes is outside the rule.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| **One plain definition, the lint finding suppressed with a citation (chosen)** | Both languages read the same type; nothing to keep equal by hand | One NOLINT per shared enum; the enum stays 4 bytes in C++ | — |
| Keep the dual head, add `VMAF_..._ABI_UINT_MAX = UINT_MAX` and `: unsigned int` | No NOLINT; the pattern `model.h` uses | An extra enumerator that every `switch` has to ignore, for an enum that is no ABI | Right for the enums that are ABI; noise for a two-value selector |
| C23 `enum E : unsigned char` in C as well | One narrow definition | MSVC does not document it for C; the Windows MSVC builds are required checks (ADR-1138) | Not buildable on a required lane |
| Leave it: the value does not cross today | No change | The next struct member or extern function that carries it fails silently, and by then the definition is far from the diff | The same pattern already failed twice in review |
| A `static_assert` on the size in each language | Catches a mismatch at compile time | Each side only sees its own size; the assert has to be written per enum | The contract test covers every header at once; the existing asserts for the ABI enums stay |

## Consequences

- **Positive**: `sizeof(VmafVifNameSet)` is the same in every translation
  unit. No score, output or ABI changes: the value was never stored or passed
  across languages.
- **Positive**: a narrow C++-only underlying type in a header C also includes
  fails a device-free test in the fast suite, with the header and the enum
  named.
- **Negative**: two clang-tidy checks are suppressed on one more enum.
- **Neutral / follow-ups**:
  - `core/src/sycl/picture_sycl.h` and `core/src/sycl/dispatch_strategy.h`
    are fixed in PR #1837 and covered by this test once both are on master.
  - The test follows quoted includes by path suffix. A header reached only
    through a generated or angle-bracket include is not seen.

## References

- `req` (coordinator brief, 2026-10-02, paraphrased): list every dual-definition enum in `core/`, say how each value crosses the language boundary, fix the unsafe ones with one definition for both languages, and add a device-free contract test that rejects a C++-only underlying type narrower than `int` on an enum a C translation unit also sees.
- [ADR-1138](1138-c-translation-units-keep-null.md) (what C cannot spell on
  the required MSVC lane), [ADR-0141](0141-touched-file-cleanup-rule.md)
  (cited suppressions), [ADR-1142](1142-whole-codebase-standards.md).
- `docs/state.md`: `T-ENUM-CXX-ONLY-UNDERLYING-TYPE-2026-10-02`.
