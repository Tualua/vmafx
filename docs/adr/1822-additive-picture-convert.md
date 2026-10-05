<!-- markdownlint-disable MD013 MD060 -->
# ADR-1822: `vmaf_picture_convert` ships additively, with the source colour as an argument

- **Status**: Proposed
- **Date**: 2026-10-05
- **Deciders**: lusoris (maintainer popup answer, 2026-10-05)
- **Tags**: api, abi, build, upstream-port, zimg, fork-local

## Context

Netflix/vmaf `0497a0f29` adds `vmaf_picture_convert()` (colourspace, pixel
format, bit depth and size conversion through zimg). It stores the colour
description in a new `VmafColor color` member of `VmafPicture`, between
`data[3]` and `ref`. That moves `ref` and `priv` and grows the struct: every
consumer built against the current header (the FFmpeg `libvmaf` filter, the Go
and Rust bindings, `VmafPicture2` conversions) reads the wrong offsets. The
public API is append-only (HISS-14, `docs/api/index.md` "ABI stability"), so a
change of that kind needs a major bump with a `Migration:` footer. The upstream
commit is also not in a Netflix release yet, so its shape may still change. The
coverage sync of 2026-10-05 stopped on it and the maintainer chose an additive
variant now.

## Decision

We ship the function and its test without changing `VmafPicture`:

- `VmafColor`, the four colour enums, `VmafResampleFilter`,
  `VmafPictureConvertTarget`, `VmafPictureConvertContext`,
  `vmaf_picture_convert()` and `vmaf_picture_convert_context_close()` are
  upstream's, with upstream's names, values and signatures.
- The one difference is the init call. Upstream reads the source colour from
  `src->color`; here the caller passes it:
  `vmaf_picture_convert_context_init_with_color(ctx, src, src_color, target)`.
  The upstream name `vmaf_picture_convert_context_init()` is not defined, so no
  symbol exists with a signature that later differs from upstream's.
- `VmafPicture` keeps its layout. `dst` of `vmaf_picture_convert()` carries no
  colour; the caller already holds it as `target.color`.
- zimg is an opt-in Meson option, `enable_zimg` (boolean, default `false`, as
  upstream). With it on, a missing zimg >= 2.7 fails `meson setup`. With it off
  the three functions return `-ENOTSUP` and log why. Default off because no
  CI image or the dev container installs zimg (checked: `dev/Containerfile`,
  `build-config.env`, `.github/workflows`), and a default dependency would
  change every consumer's build.
- The code lives in `core/src/picture_convert.c`, a separate translation unit,
  not in `picture.c`: `picture.c` is compiled into some 40 test executables,
  none of which should need zimg or the logger. `picture_convert_lib` is built
  once and linked into libvmaf and the two tests.
- The carrier is an explicit argument, not `VmafPicture2::_reserved`:
  `VmafPicture2` is produced and consumed by nothing in the conversion path,
  `vmaf_picture_v1_to_v2()` would have to invent a colour, and `_reserved` is
  integers a future field must be able to claim.
- Guard: `core/test/test_picture_convert_api.c` holds the order and offsets of
  every `VmafPicture` member with `_Static_assert`s (a build break if a colour
  field is inserted), and checks the `-ENOTSUP` contract without zimg. The
  repository has no abi-compliance or symbol-diff gate; this test is the
  layout proof.

When upstream releases the function (and its `VmafPicture::color`), the fork
takes one of two paths in that release's ADR: with an ABI-breaking major bump
for `VmafPicture` already planned (ADR-0928), adopt upstream's init signature
and drop `_with_color`; otherwise keep `_with_color` and add an
upstream-compatible `vmaf_picture_convert_context_init()` wrapper that reads
the colour from a `VmafPicture2` or a documented side channel, with
`_with_color` documented as the stable form. Either way no released symbol is
removed without the deprecation window of `docs/api/index.md`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
| --- | --- | --- | --- |
| Port upstream's layout (`VmafColor` inside `VmafPicture`) with a SONAME / major bump | Identical to upstream; no later migration | Binary break for every consumer now, for a function upstream has not released | Maintainer chose the additive variant |
| Append `VmafColor` after `priv` | Old members keep their offsets | Still a size change: stack-allocated pictures and array strides break; diverges from upstream's layout anyway | Not additive in the ABI sense |
| Carry the colour in `VmafPicture2::_reserved` | No new argument | `VmafPicture2` is not on the conversion path; v1 pictures promote without a colour; spends reserved space | Explicit argument is simpler and honest |
| Keep upstream's `..._context_init()` name with a different signature | Fewer new names | Same symbol, different prototype from upstream once released: silent mismatch for anyone porting code | A distinct name makes the difference visible |
| Default `enable_zimg=true` | Function works out of the box | New mandatory dependency, absent from CI images and the dev container | Opt-in as upstream; fails closed at configure time |
| Skip the port | No divergence | The coverage gap stays | Maintainer asked for it |

## Consequences

- **Positive**: libvmaf gains zimg conversion with no ABI change; the layout
  guard makes any later accidental change of `VmafPicture` fail a test; the
  default build and the Netflix golden gate are untouched.
- **Negative**: the init signature differs from upstream's until upstream
  releases; a rebase of `picture.c` and `picture.h` hunks of `0497a0f29` must
  not restore `VmafPicture::color` (rebase-notes entry).
- **Neutral / follow-ups**: the FFmpeg patch series needs no edit (no existing
  symbol or struct changes; `ffmpeg_patch_stack.py --check` run in the PR).
  CI and the dev container do not build with zimg yet; adding it is a pinned
  dependency change for a separate PR (supply chain, HISS-11). The tests with
  zimg run where zimg >= 2.7 is installed (`-Denable_zimg=true`).

## Supply-chain impact

- **New dependencies**: zimg >= 2.7 (`build` and `runtime`, only with
  `-Denable_zimg=true`; MIT-style licence; <https://github.com/sekrit-twc/zimg>).
  Absent from every default build.
- **Build-time fetches**: none.
- **CVE surface delta**: none by default; with the option on, an image-processing
  library is linked.

## References

- Maintainer popup answer, 2026-10-05, option "Additive variant now" (paraphrased: port
  `vmaf_picture_convert` and `test_colorspace` without changing `VmafPicture`'s layout).
- Netflix/vmaf `0497a0f29` "libvmaf: add vmaf_picture_convert api".
- [ADR-0928](0928-vmaf-picture-v2-explicit-backend-state.md),
  [ADR-1487](1487-upstream-parity-policy-and-guard.md), `docs/api/index.md` "ABI stability".
