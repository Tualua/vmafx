<!-- markdownlint-disable MD013 MD060 -->
# ADR-1852: A new VMAFx C API generated from one definition, libvmaf as its compatibility layer, and VMAFx-named FFmpeg filters (RC4)

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: maintainer
- **Tags**: api, abi, rc4, ffmpeg, bindings, codegen, compat, provenance, roadmap

## Context

The fork exposes one engine through many hand-maintained surfaces: 107
exported C functions in 13 `core/include/libvmaf/*.h` headers, a bindgen Rust
binding with a hand-written safe layer, three Go paths into libvmaf (CLI
subprocess, direct cgo, stream cgo), Python consumers that all run the CLI,
a gRPC proto and an OpenAPI file for one server, two MCP servers whose tool
schemas are kept equal by parity tests, a CLI option table with a separate
usage text, and twenty FFmpeg patches with five filters and their AVOption
tables. The full inventory, with the drift found in each surface, is
[Research-2158](../research/2158-vmafx-api-redesign.md).

Three facts make the current shape expensive. The descriptions of one
scoring request drift (both IDLs of the server document a default model the
library no longer uses; the Go MCP parity test compares against a copied
list). Provenance is a CLI feature: the CLI splices `backend_used` and
`feature_backends` into the JSON file after libvmaf wrote it, so no API user
(the FFmpeg filters, the bindings) gets it (#2142). And RC4 adds the
zero-copy import API with fences ([ADR-1829](1829-rc4-zero-copy-import.md)),
asynchronous window scores (#2138) and a versioned server contract (#2155),
each of which would add another hand-written surface per language.

[ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md) decided "no new
header family" and an additive API on `libvmaf.h`;
[ADR-0686](0686-vmafx-rebrand-aggressive-modernization.md) kept the library and
the FFmpeg filter names to protect downstream builds. On 2026-10-05 the
maintainer decided that the cheapest moment for a new API, and for VMAFx names
on every FFmpeg filter, is before `v1.0.0`, as long as existing users keep
working through a compatibility layer.

## Decision

1. **New VMAFx C API, libvmaf as its compatibility layer.** New headers
   `vmafx/*.h`, prefix `vmafx_`, library `libvmafx.so.1` (pkg-config
   `libvmafx`; ABI 1.0.0 frozen at the `v1.0.0` tag, its number line
   independent of the product version). The API is built around contexts,
   devices, refcounted frames with acquire and release fences (the RC4 import
   API of ADR-1829 is this frame object), synchronous and asynchronous window
   scores with `vmaf_score_pooled` semantics, a provenance record behind every
   score, size-prefixed structs, opaque handles and errors that name what
   failed. Status codes are the API's own `VMAFX_*` values with a generated
   errno map for the compatibility layer.
2. **libvmaf stays, as a separate thin library.** `libvmaf.so.3` links only the
   exported `vmafx_` symbols of `libvmafx.so.1` and implements every
   `libvmaf.h` function on them, so upstream FFmpeg's own filter, the CLI, the
   Python harness and existing users keep working and the new API is proven
   complete at link time. `libvmaf.h` is deprecated: warnings are opt-in in
   1.0 (`VMAF_ENABLE_DEPRECATION_WARNINGS`), on by default in 1.1, and the
   library is removed in 2.0.
3. **One definition generates every surface.** `core/api/vmafx.toml`, read by
   standard-library Python generators under `scripts/codegen/`, produces the C
   headers, the compatibility shims, the bindings, the ABI layout tests, the
   option tables of the CLI, FFmpeg, MCP and gRPC, and the reference docs.
   Generated files are committed; a test fails when one differs from the
   definition, and an append-only check refuses a breaking change without a
   higher ABI major (HISS-14).
4. **FFmpeg: every filter capability survives under a VMAFx name, none under a
   `vmaf` name.** `vmafx` scores software frames and CUDA, SYCL, HIP and Metal
   hardware frames with `backend` and `device` options (upstream option names
   kept as aliases), per-window statistics (`n_stats`, #2138) and provenance.
   `vmafx_tune` replaces `libvmaf_tune`, `vmafx_pre` replaces `vmaf_pre`, and
   the `-vmaf-profile` CLI option becomes `-vmafx-profile`. The old names are
   removed in the same change, with a migration guide. Each old filter, its
   new name and its option mapping are listed below.
5. **Import rule.** A transient import failure is retried once after a host
   wait on the acquire fence; any other failure, or a second one, fails the
   filter graph naming the backend, input, formats, plane and every refusing
   extractor. No frame passes unscored and nothing falls back silently to a
   host copy.
6. **Phase: RC4**, together with the zero-copy import API and the Rust metric
   of #1723. RC4 code changes stay drafts with the `rc4` label until the
   `v1.0.0-rc.3` tag (ADR-1341); this decision record lands on its own.

FFmpeg names after the change:

| Old name (patches) | New name | Options: old -> new |
|---|---|---|
| `libvmaf` (FFmpeg's filter + 0001, 0003, 0004, 0010, 0011, 0012, 0014, 0016, 0017, 0018, 0020) | `vmafx` | `model`, `feature`, `log_path`, `log_fmt`, `pool` (incl. `perc5` / `perc10` / `perc20`), `cpumask`, `gpumask`, `score_fmt`, `perceptual_weight`, `tiny_model`, `tiny_device`, `tiny_threads` unchanged; `n_threads` -> `threads` and `n_subsample` -> `subsample` (old names kept as aliases); `cuda=1` -> `backend=cuda`; `sycl_device=N` -> `backend=sycl:device=N`; `hip_device=N` -> `backend=hip:device=N`; `metal_device=-2` / `-1` / `N` -> `backend=cpu` / `backend=metal` / `backend=metal:device=N`; `sycl_profile=1` -> `profile=1`; `vulkan_device` -> Vulkan frames row |
| `libvmaf_cuda` (FFmpeg's filter) | `vmafx` | Same as `libvmaf`; CUDA hardware frames select the CUDA backend and are imported without the device-to-device copy |
| `libvmaf_sycl` (0005) | `vmafx` | `log_path`, `log_fmt`, `pool`, `model`, `feature`, `cpumask`, `gpumask`, `score_fmt` unchanged; `n_threads` / `n_subsample` as above; `gpu_profile` -> `profile`; hardware frames as DRM PRIME (or mapped with `hwmap`), luma and chroma |
| `libvmaf_metal` (0013) | `vmafx` | Same as `libvmaf_sycl` without `gpu_profile`; macOS hardware frames bound without the CPU copy |
| `libvmaf_vulkan` (0006; builds no code since [ADR-0726](0726-drop-vulkan-backend.md)) | `vmafx` | Same as `libvmaf_sycl` without `gpu_profile`; Vulkan frames mapped to DRM PRIME (`hwmap=derive_device=drm`) and scored on SYCL or HIP |
| `libvmaf_tune` (0008) | `vmafx_tune` | `model`, `feature`, `recommend_target_vmaf`, `recommend_crf_min`, `recommend_crf_max`, `recommend_passes` unchanged; `n_threads` -> `threads` (alias kept); new `backend`, `device`; default model becomes the library default |
| `vmaf_pre` (0002) | `vmafx_pre` | `model`, `device`, `threads`, `chroma` unchanged |
| `-vmaf-profile` (0015) | `-vmafx-profile` | Same argument |

`-pass-autotune` (0009), `-qpfile` on three encoders (0007) and the build
diagnostics patch (0019) carry no `vmaf` name and stay. The fork's FFmpeg
builds configure `--enable-libvmafx` only; unpatched upstream FFmpeg keeps
building its own `libvmaf` filter against the compatibility layer.

## Alternatives considered

| Question | Option | Pros | Cons | Outcome |
|---|---|---|---|---|
| API | New `vmafx` API, `libvmaf.h` as compat layer (**chosen**) | One coherent model for contexts, devices, fences, windows, provenance and errors before `v1.0.0` freezes the ABI; existing users unaffected | Two header families until 2.0; compat layer to maintain and test | Chosen |
| API | Additive functions on `libvmaf.h` (ADR-1685) | No new names | The import API, windows and provenance land as more `vmaf_*` families beside the four `*_state_init` ones; by-value config structs cannot grow | Not chosen |
| API | Rename and break in place | One API | Breaks upstream FFmpeg, distributions and every binding at once (ADR-0686) | Not chosen |
| Generation | In-house TOML definition + Python generators (**chosen**) | Describes a C ABI with handles, fences and callbacks and the non-binding surfaces; nothing new at build time; no licence on output | The emitters are fork code to maintain | Chosen |
| Generation | Rust source with cbindgen + Diplomat / UniFFI | Mature binding generators | The public API's implementation must be Rust; CLI, FFmpeg, MCP tables not covered; UniFFI's FFI is not a public C ABI | Not chosen |
| Generation | protobuf + custom plugins | `buf breaking`, gRPC for free | No handles, pointers, out-parameters or callbacks without custom options | Not chosen |
| Generation | Annotated C headers + libclang | C stays the truth | Option groups, MCP and FFmpeg tables need a second source | Not chosen |
| Generation | WIT + wit-bindgen | Component model | WebAssembly components only; CLI declared unstable | Not chosen |
| SONAME | `libvmafx.so.1`, frozen at `v1.0.0` (**chosen**) | Clear start of the new line | New package name for distributions | Chosen |
| SONAME | `libvmafx.so.4` continuing libvmaf's line | Signals succession | Couples two unrelated number lines | Not chosen |
| Compat packaging | Separate thin `libvmaf.so.3` (**chosen**) | Existing binaries keep loading; new-API completeness is a link-time fact; 2.0 drops one library | Two libraries to package | Chosen |
| Compat packaging | One library exporting both, `libvmaf.pc` redirected | One file | Binaries linked to `libvmaf.so.3` must be relinked | Not chosen |
| Compat packaging | Both APIs in `libvmaf.so.3` | Smallest change | No `libvmafx` library | Not chosen |
| Status codes | Own `VMAFX_*` codes + errno map (**chosen**) | Same numbers on every platform | One mapping table to generate | Chosen |
| Status codes | Negative errno | Today's convention | errno values differ between C runtimes | Not chosen |
| FFmpeg | Every capability under a VMAFx name, old names removed now (**chosen**) | One family of names; nothing users rely on is lost | Every user command changes once | Chosen |
| FFmpeg | Retire `libvmaf_tune`, keep CRF recommendation in the vmaf-tune CLI only | One filter fewer | Drops a capability users have | Not chosen |
| FFmpeg | Keep old filter names next to the new ones for a while | Gentler migration | Two filter generations through RC4-RC9 | Not chosen |
| Deprecation | Opt-in 1.0, default 1.1, gone 2.0 (**chosen**) | Upstream FFmpeg builds without new warnings in 1.0 | Users learn of it later | Chosen |
| Deprecation | Warnings on by default in 1.0 | Earlier signal | New warnings in every downstream build at once | Not chosen |
| Import failure | One retry, then fail named (**chosen**) | Covers transient fence time-outs; never a silent copy | One extra host wait on a failing path | Chosen |
| Import failure | Fail at once | Simplest | A transient time-out kills a long encode | Not chosen |
| Phase | RC4 with zero-copy and the Rust metric (**chosen**) | The import API is designed once, on the new frame object | RC4 grows again | Chosen |
| Phase | After `v1.0.0` | Smaller RC4 | The ABI frozen at `v1.0.0` would be the old one | Not chosen |

## Consequences

- **Positive**: the ABI frozen at `v1.0.0` is designed for device frames,
  fences, windows and provenance; every binding and option table follows one
  definition and a hand edit fails a test; the separate compatibility library
  proves the new API complete; provenance reaches every consumer; one family
  of FFmpeg names covers every capability.
- **Negative**: RC4 grows (API, generator, compatibility library, three
  filters and a CLI option on top of the Rust metric and the import API); the
  emitters are fork code; every FFmpeg user command changes once.
- **Neutral / follow-ups**: RC4 work packages WP1-WP12 of Research-2158 (the
  prototype slice is draft PR #2173); [ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md)
  is superseded in part (its API-shape item) and
  [ADR-0686](0686-vmafx-rebrand-aggressive-modernization.md) in part (the
  library and filter names); the FFmpeg patch series follows rule 11 in the
  same change as the filters.

## References

- `Q`: "New vmafx API + libvmaf compat (Recommended)"
- `Q`: "Own filter, retire old ones now"
- `Q`: "RC4, with zero-copy (Recommended)"
- `Q` (D1): "In-house TOML + Python gens (Recommended)"
- `Q` (D2): "libvmafx.so.1 (Recommended)"
- `Q` (D3): "Separate thin libvmaf.so.3 (Recommended)"
- `Q` (D4): "vmafx, backend+device (Recommended)"
- `Q` (D5): "Own VMAFX_* codes + errno map (Recommended)"
- `Q` (D6, custom answer, verbatim): "we still want all ffmpeg filters? lol but not as vmaf anymore lol"
- `Q` (D7): "Opt-in 1.0, default 1.1, gone 2.0 (Recommended)"
- `Q` (D8): "One retry, then fail named (Recommended)"
- `req` (verbatim): "okay when I read this I think its time to change from vmaf to vmafx own ffmpeg shit and to insta redesign the api for my whole project because the sooner the better then"
- `req` (verbatim): "we could gen the api? and then actually automate a lot by this?"
- Issues [#1723](https://github.com/VMAFx/vmafx/issues/1723), [#2067](https://github.com/VMAFx/vmafx/issues/2067), [#2138](https://github.com/VMAFx/vmafx/issues/2138), [#2142](https://github.com/VMAFx/vmafx/issues/2142), [#2155](https://github.com/VMAFx/vmafx/issues/2155)
- [ADR-0686](0686-vmafx-rebrand-aggressive-modernization.md), [ADR-0726](0726-drop-vulkan-backend.md), [ADR-0928](0928-vmaf-picture-v2-explicit-backend-state.md), [ADR-1199](1199-cuda-picture-handover-barrier.md), [ADR-1336](1336-cuda-context-owned-resource-teardown.md), [ADR-1679](1679-metal-iosurface-biplanar-import.md), [ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md), [ADR-1688](1688-sycl-zero-copy-luma-only-admission.md), [ADR-1829](1829-rc4-zero-copy-import.md)
- Research: [Research-2158](../research/2158-vmafx-api-redesign.md)
