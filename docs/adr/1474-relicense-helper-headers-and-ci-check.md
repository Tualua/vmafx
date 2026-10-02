<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1474: A fork-created header that replays upstream arithmetic is `EUPL-1.2 AND` the licences of exactly that code, and the relicensing check becomes a required CI job

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `license`, `compliance`, `ci`, `rc3`, `fork-local`

## Context

[ADR-1250](1250-eupl-fork-relicense.md) decides every file's licence by
provenance and makes the decision executable:
`scripts/dev/relicense_fork_files.py --check` fails when a file's header
disagrees with it. ADR-1250 left the check out of CI ("until it is, a new
fork-authored file can arrive with the wrong tag and nothing will say so"),
and two weeks of work showed what that costs: on 2026-10-02 the check reported
41 pending files on master. Ten were fork files with the wrong tag (PR #1876).
The other 31 exposed three things ADR-1250 does not settle.

**Helper headers created under EUPL-1.2 inside kernel directories.** ADR-1250
says a file that carries someone else's code "keeps the terms it has" and does
not move to EUPL-1.2. Seven headers were created after the relicensing with an
`EUPL-1.2` tag in a kernel directory whose family names an upstream origin, so
the tool wanted to add that origin's notices to all seven. Read against their
sources they are two kinds:

- three reproduce a piece of a reference extractor's arithmetic for a GPU twin:
  `hip/float_ssim/ssim_decimate.h` (the boundary rule and window loop of
  `iqa/convolve.c` and `iqa/decimate.c`, on `picture_copy()` values with
  `ssim.c`'s box tap), `metal/float_ms_ssim_option_semantics.h` (the `max_db`
  formula of `float_ms_ssim.c`) and `sycl/sycl_integer_ssim_math.h` (the
  per-pixel term of `integer_ssim.c::ssim_reduce_row_range()` on 64-bit
  integers). They have EUPL terms, so "keeps the terms it has" keeps EUPL; they
  carry upstream code, so the upstream notice and licence are owed. The tool's
  answer, the upstream notices plus `EUPL-1.2 AND <upstream licences>`, was one
  of three readings;
- four hold none of the reference's code: `cuda/speed/speed_cuda_params.h` is
  the kernel argument block and thread counts of the fork's CUDA SpEED chain,
  and `hip/float_adm/float_adm_hip_math.h`, `hip/integer_ciede/ciede_hip_math.h`
  and `sycl/sycl_ciede_math.h` define the device's primitives as macros and
  include a shared header (`float_adm_gpu_common.h`, `ciede_ff_math.h`) that
  holds the arithmetic and already carries the upstream notices. A notice added
  to them would name a copyright holder for code that is not in the file.

All paths are under `core/src/feature/`.

**Family rules are coarser than helper headers.** A family credits a kernel
file with every origin of its metric: the CUDA, HIP and SYCL SSIM files cross
the float (Netflix, IQA) and integer (Xiph.Org) lineages, so the `gpu-ssim`
family names all three. A helper header holds one part. The family would have
credited `sycl_integer_ssim_math.h` (the integer term only) to Netflix and Tom
Distler, `ssim_decimate.h` and `sycl_ssim_terms.h` (IQA and Netflix float code)
to Xiph.Org, and, through a pattern that needs the file name to start with
`ssimulacra2`, `sycl_ssimulacra2_math.h` to all three instead of libjxl.

**Files the tool misread.** The `scripts/ci/exact_twins.d/*.hip` fragments are
data whose suffix names a backend; two files are praetor's byte-locked managed
files; `scripts/sync-pelorus-interop.sh` embeds the header of the mirrored
Pelorus files as a string. The tool wanted to give the first two groups a
header and to rewrite the third's template.

## Decision

1. **A fork-created header that replays upstream arithmetic carries
   `EUPL-1.2 AND` the licences of the code it reproduces**, with that code's
   copyright notices added above the fork's, which stays. The expression lists
   exactly the licences of the code actually reproduced, read from the header's
   file read against its source, not the family default. This is the three
   headers of the first kind.
2. **A fork-created header that holds none of the reference's code stays
   `EUPL-1.2`**, with a `[not_ports]` entry that says why: an argument block,
   or macros and an include of a shared header that carries the notices itself.
   This is the four headers of the second kind; they are not changed.
3. **A `[ports]` entry names a helper header's origins when its family is
   coarser than the header.** Families remain the default for kernel files.
4. **Fork-added headers that carry only an upstream notice stay as they are**
   (`core/src/feature/integer_ssim.h`, `core/src/feature/iqa/ssim_simd.h`): no
   fork notice is added.
5. **The tool does not manage** exact-twin fragments, praetor's byte-locked
   files, or a licence grant below a file's own header (the first 60 lines).
6. **`relicense_fork_files.py --check` is a required CI job.** It runs against
   the Netflix/vmaf commit the repository already records as the upstream head
   it is at parity with (the heading of that name in
   `docs/development/known-upstream-bugs.md`), with the full history it needs
   for the author veto. The pin moves where it always moved: with an upstream
   sync or port.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| All seven stay `EUPL-1.2` (`[not_ports]`) | Nothing changes | Three of them reproduce reference arithmetic line for line and say so; the notices of that code would not travel with it | The licences of the reproduced code require the notice |
| The three become `BSD-2-Clause-Patent AND ...`, dropping EUPL, like their sibling kernels | One convention for every file in a kernel directory | Removes terms the files were published under and the fork's reciprocity on its own part of them | Rejected by the maintainer in favour of the mixed expression |
| `EUPL-1.2 AND <upstream licences>` for the three (**chosen**) | Credits the reproduced code, keeps the terms the fork gave its own part | A mixed expression per file | The maintainer's decision |
| The mixed expression for all seven, by family | One rule per directory, no per-file review | Adds Netflix's and Joshua Holmer's copyright notices to four files that hold none of their code | A tag and its notices describe the file; the maintainer confirmed `EUPL-1.2` for the four |
| Widen the `ssimulacra2` family pattern instead of `[ports]` entries | One line | Changes how every future file with `ssimulacra2` in its name resolves; does nothing for the three SSIM headers | Per-file entries change exactly the files reviewed |
| Run the check as a pre-commit hook | Feedback before the push | It reads every candidate's history (265 CPU seconds, about 30 s on 16 jobs) and needs the upstream tree | Too slow and too history-dependent for a commit hook; the tag-against-notice scan is the pre-commit side |
| Run the check against upstream's moving `master` | No pin to maintain | A file added upstream would turn the job red on an unrelated pull request | The pin moves with the reviewed upstream sync |

## Consequences

- **Positive**: `--check` exits 0 and stays there; a new fork file with the
  wrong tag, a new helper header without its upstream notice, and a stale
  provenance entry fail a required job; the three headers credit the code they
  reproduce, and no header credits code it does not hold.
- **Negative**: one more required job with a full-history checkout; an
  upstream sync has one more value to move.
- **Neutral / follow-ups**: a helper header added to a kernel directory needs a
  `[ports]` entry when its family names origins it does not reproduce and a
  `[not_ports]` entry when it reproduces none; the job says so when it fails.

## References

- [ADR-1250](1250-eupl-fork-relicense.md): the provenance rule and the tool.
- [ADR-1351](1351-praetor-engine-pin-move.md): praetor's byte-locked files.
- [ADR-1428](1428-exact-twins-fragments.md): the exact-twin fragment files.
- `T-RELICENSE-CHECK-PENDING-2026-10-02`, PR #1876 (the 41 pending entries).
- Source: popup answer of the maintainer, relayed by the session coordinator on
  2026-10-02. Q: how the seven EUPL-tagged helper headers that replay upstream
  arithmetic are licensed. A: "Mixed: EUPL-1.2 AND the upstream licence". Given
  for the group of seven, before the per-file check.
- Source: popup answer of the maintainer, relayed by the session coordinator on
  2026-10-02. Q: asked with the per-file findings, what the four headers that
  contain no upstream code carry. A: "EUPL-1.2 only (Recommended)". The
  correction for the four; the three that reproduce code keep the mixed
  expression.
- Source: popup answer of the maintainer, relayed by the session coordinator on
  2026-10-02. Q: whether the fork-added `integer_ssim.h` and `iqa/ssim_simd.h`
  also credit the fork. A: "Leave them as they are".
