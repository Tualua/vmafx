<!-- markdownlint-disable MD013 MD060 -->
# Research-2158: VMAFx API redesign, generated surfaces and the VMAFx FFmpeg filters

- **Status**: Active
- **Workstream**: [ADR-1852](../adr/1852-vmafx-api-redesign.md), [ADR-1829](../adr/1829-rc4-zero-copy-import.md), ADR-1713 (draft PR #2086; RC4, #1723)
- **Last updated**: 2026-10-05

Design review for RC4: an inventory of every API surface the repository has
today, the model of the new C API, the comparison of definition-driven
generators, the VMAFx FFmpeg filters, the work plan, and the maintainer's
answers to the open decisions (section 7). Read at `origin/master`
`8eabe7c56` (function counts updated to `9bc68a108`); issues [#1723],
[#2067], [#2138], [#2142] and [#2155]; ADR-0686, ADR-0928, ADR-1199,
ADR-1685, ADR-1829, ADR-1755 (PR #2108), ADR-1688, ADR-1679; HISS-14 in
`AGENTS.md`.

[#1723]: https://github.com/VMAFx/vmafx/issues/1723
[#2067]: https://github.com/VMAFx/vmafx/issues/2067
[#2138]: https://github.com/VMAFx/vmafx/issues/2138
[#2142]: https://github.com/VMAFx/vmafx/issues/2142
[#2155]: https://github.com/VMAFx/vmafx/issues/2155

## 0. Summary

1. **One API definition generates every surface.** Recommended source of truth: an in-house interface definition in TOML (`core/api/vmafx.toml`), read by small generators written in standard-library Python (`scripts/codegen/vmafx_api/`). It is the only candidate that describes a C ABI with opaque handles, out-parameters, fences and callbacks *and* the non-binding surfaces (CLI table, FFmpeg option table, MCP schemas, proto messages, docs) without fighting its own type model, needs nothing at build time beyond the Python Meson already runs, and carries no licence question. Generated files are committed; a Meson test regenerates them and fails on any difference, so a hand edit fails.
2. **New C API** under `vmafx/*.h`, prefix `vmafx_`, library `libvmafx`: contexts, devices, refcounted frames with acquire and release fences (the RC4 import API is this frame object), synchronous and asynchronous window scores with `vmaf_score_pooled` semantics, a provenance record behind every score, and errors that name what failed. Size-prefixed structs, opaque handles, explicit enum values, stable status codes.
3. **Compatibility layer**: every one of the 107 exported `libvmaf` functions is listed in section 2.11 with its `vmafx_` target and whether its shim is generated or written by hand. Recommended packaging: a thin `libvmaf.so.3` that links only the public symbols of `libvmafx.so.1`, which mechanically proves the new API is complete. Upstream FFmpeg's own `libvmaf` and `libvmaf_cuda` filters, the CLI, the Python harness and the golden-data gate keep working through it.
4. **One `vmafx` scoring filter** on the new API (plus `vmafx_tune` and `vmafx_pre`, D6): software frames and hardware frames for CUDA, SYCL, HIP and Metal, `n_stats` windows by time or frame count, provenance in the log, a retry-then-fail-named import rule and no silent host copy. Every capability of today's 20 patches survives under a VMAFx name and no filter keeps a `vmaf` name (section 5.9, D6); section 5.7 maps every option of every retired filter.
5. **Prototype**: the generator on a slice (context create and destroy, version and provenance query, one feature-score call), producing the C header, the compat shims for `vmaf_init`, `vmaf_close`, `vmaf_version` and `vmaf_feature_score_at_index`, a Python binding, the ABI layout test and a drift check, wired into Meson.

## 1. Inventory of today's API surfaces

### 1.1 Table

| Surface | Where | What it exposes | How it is kept in sync today | Known drift |
| --- | --- | --- | --- | --- |
| C public API | `core/include/libvmaf/*.h` (13 headers with exports; `pelorus/*.h` export nothing and are not installed) | 104 `VMAF_EXPORT` functions at `8eabe7c56`: `libvmaf.h` 21, `libvmaf_sycl.h` 20, `model.h` 12, `dnn.h` 12, `libvmaf_metal.h` 9, `libvmaf_mcp.h` 8, `libvmaf_cuda.h` 5, `libvmaf_hip.h` 5, `picture_v2.h` 5, `perceptual_weight.h` 3, `feature.h` 2, `picture.h` 2; 107 at `9bc68a108` (`picture.h` gained `vmaf_picture_convert_context_init_with_color`, `vmaf_picture_convert`, `vmaf_picture_convert_context_close`, #2140) | Hand-written. `core/test/check_exported_symbols.py` (ADR-0379) fails when the `.so` exports a symbol no public header declares; `scripts/ci/ffmpeg-patches-surface-check.sh` greps the patch series for consumed names | No ABI check across releases (no `abidiff`, no layout test). Structs passed by value (`vmaf_init(VmafConfiguration)`, `vmaf_*_state_init(cfg)`) cannot grow. Backend-specific shapes for the same idea: four `*_state_init`, three `*_list_devices`, two preallocation enums, three imports (`vmaf_sycl_import_va_surface`, `vmaf_sycl_dmabuf_import`, `vmaf_metal_picture_import`) with different ownership rules. `VmafBackend` still lists `VMAF_BACKEND_VULKAN` (ADR-0726 removed the backend). Logging is process-global (`vmaf_set_log_level` from `vmaf_init`). Errors are bare negative errno values with the detail only in the log |
| Picture v2 | `core/include/libvmaf/picture_v2.h` | `VmafPicture2` with backend tag and handle, converters | ADR-0928, phases 2-4 still Proposed | Declared and installed, but no backend reads `backend_handle`; no fence |
| Rust | `bindings/rust/vmafx-sys` (bindgen at build time from `${LIBVMAF_PREFIX}/include/libvmaf/libvmaf.h`), `bindings/rust/vmafx` (hand-written safe layer), `core/src/rust/**` (RC4 extractor framework, internal ABI from cbindgen, ADR-1713) | `vmaf_*` FFI; `Context`, `Model`, `Picture`, `Score`, `Error` | bindgen output follows the installed header; the safe layer is maintained by hand | bindgen needs libclang and an external crate at build time. Only `libvmaf.h`'s include closure is bound (no GPU headers). The safe layer's errno mapping (5 values, `bindings/rust/vmafx/AGENTS.md`) differs from Go's (4 sentinels). Ownership rules re-derived by hand (ADR-1755 needed fixes in both crates, PR #2108) |
| Go | `pkg/libvmaf` (cgo: `libvmaf.go`, `direct.go`, `stream.go`, `dnn.go`), `pkg/scorecli`, `pkg/scorebackend`, `cmd/vmafx-server`, `cmd/vmafx-mcp` | Three paths: subprocess to the `vmaf` CLI, direct cgo file scoring (ADR-0931), cgo stream scoring for gRPC `ScoreStream` (ADR-0933) | Hand-written cgo; `pkg/libvmaf/AGENTS.md` pins the picture-ownership and errno contracts | `pkg/libvmaf/doc.go` still says the package "does not link against libvmaf.so at runtime" while `direct.go` and `stream.go` do. `pkg/scorebackend` is a hand port of `tools/vmaf-tune/src/vmaftune/score_backend.py` (two implementations of backend selection, HISS-19) |
| Python | `compat/python-vmaf` (harness, `ExternalProgram` runs the CLI), `mcp-server/vmaf-mcp` (subprocess), `ai/` (CLI), `tools/vmaf-tune` (CLI) | Nothing binds the C API; every Python consumer runs the CLI and parses its JSON | The CLI's JSON shape is the de facto Python API | No schema for the JSON output; consumers each parse it (`server.py` looks up `vmaf`, `vmaf_v0.6.1`, `vmaf_v0.6.1neg`, `vmaf_4k_v0.6.1` keys in turn) |
| gRPC / HTTP | `proto/vmafx.proto` (`vmafx.v1`: `Score`, `ScoreStream`, `Health`), `buf.gen.yaml`, `gen/go/*.pb.go` (+ `scripts/proto/postprocess_gen_go.py` for HISS-09 proofs); `api/openapi/vmafx-server-v1.yaml` + `oapi-codegen` -> `gen/go/oapi` | Score request/response, stream frames, health | Two hand-written IDLs for one server; `buf generate` and `oapi-codegen` output committed | Both IDLs document `vmaf_v0.6.1` as the default model while the library default is `vmaf_v1.0.16_3d0h` (`model.h`). `PixelFormat` in the proto is a different enum from `VmafPixelFormat` (no 4:0:0, bit depth folded in). No provenance field anywhere (#2142, #2155). The proto header comment says stubs live under `gen/go/vmafx/`; they are in `gen/go/` |
| MCP | `cmd/vmafx-mcp/tools.go` (Go, 24 tools), `mcp-server/vmaf-mcp/src/vmaf_mcp/server.py` (Python) | Tool names and JSON input schemas | Two hand-written schema sets; `TestToolListMatchesPython`, `TestToolSchemasMatchPython`, `TestGoAndPythonArgvParity` / `test_parity_argv.py` (ADR-1117) | `TestToolListMatchesPython` compares the Go server against a Go literal copy of the Python list (`pythonToolNames`), not against the Python server. Both servers build the CLI argv by hand and pin it against `core/tools/cli_parse.cpp` |
| CLI | `core/tools/cli_parse.cpp` (`long_opts[]`, 64 entries incl. aliases), `print_usage_*()` | Every scoring option | Option table and usage text are separate hand-written lists | Usage text and table drift independently (the help still mentions Vulkan); dash and underscore aliases are added one by one; MCP servers duplicate the flag spellings |
| FFmpeg | `ffmpeg-patches/` (20 patches against `n9.0.2`, `series.txt`) | Filters `libvmaf` and `libvmaf_cuda` (upstream, patched), `libvmaf_sycl` (0005), `libvmaf_metal` (0013), `libvmaf_vulkan` (0006, compiles to nothing since ADR-0726), `libvmaf_tune` (0008), `vmaf_pre` (0002) | Hand-written AVOption tables per filter; `cpumask` / `gpumask` (0014) and `score_fmt` (0016) are pasted into four tables each; `scripts/ci/ffmpeg_patch_stack.py --check` replays the series | `metal_device` uses -2 / -1 / N while `sycl_device` / `hip_device` use -1 / N; `gpumask` help says "1=disable CUDA" only; `vulkan_device` survives as an option of a removed backend; device selection is a different option per backend; no hardware-frame path except `libvmaf_sycl` (luma only, ADR-1688) and `libvmaf_metal` (CPU copy, ADR-0423); no per-window statistics (#2138) |
| Docs | `docs/api/*.md` (hand-written: `index`, `lifecycle`, `pictures`, `models-and-features`, `gpu`, `dnn`, `mcp`, ...), `docs/mcp/tools.md`, `docs/usage/cli.md`, `docs/usage/ffmpeg.md` | Prose reference | Review; `test_gpu_public_header_docs.py` for GPU headers | Every signature is restated by hand in at least one page |
| Output formats | `core/src/output.cpp` (`vmaf_write_output*`), `core/tools/vmaf.cpp` (`amend_cli_backend_receipt`) | XML, JSON, CSV, SUB | Hand-written | Provenance is spliced into the JSON file by the CLI after libvmaf wrote it (`backend_used`, `feature_backends`, ADR-1359). XML / CSV / SUB never carry it, and neither does any report written by an API user (the FFmpeg filters). No model name or hash in any format (#2142) |

### 1.2 Observations that shape the design

- **Six hand-maintained descriptions of one scoring request** (CLI table, CLI usage, Go MCP schema, Python MCP schema, proto, OpenAPI) plus four FFmpeg option tables. The parity tests exist because the descriptions drift; generating them from one definition removes the drift instead of detecting it.
- **No binding reaches the C API from Python**, and Go reaches it three different ways. A generated binding per language replaces bindgen at build time (Rust) and the hand cgo (Go).
- **Provenance is a CLI feature, not a library feature.** It must move into the library so every consumer (filter, bindings, server) gets the same record.
- **The import paths already learned the hard way that failures must name what failed** (ADR-1688: `-ENOTSUP` naming each refused extractor; ADR-1679: a frame that cannot be imported fails the filter instead of passing through unscored) and that a hand-over without an ordering primitive races (ADR-1199). The new frame object makes both rules part of the type.

## 2. API model

### 2.1 Principles

1. Every object with a lifetime is an opaque handle with a create and a release function. No caller-visible struct holds library-owned memory.
2. Every struct that crosses the ABI starts with `uint32_t struct_size` and only grows at the end (HISS-14). No struct is passed by value except `VmafxFence` (fixed, sized). No `bool`, `long`, bit-field or C `enum` as a field type: enums are `uint32_t` fields with named constants of explicit value.
3. Every fallible function returns `VmafxStatus` and takes an optional `VmafxError **error` last.
4. One function per concept across backends: one device constructor, one frame import, one fence type, one pool. Backend specifics live in tagged descriptors, never in new function families.
5. No silent fallback (user rule): a requested backend, device or import path that is unavailable fails with a named error, and every result records which path ran.
6. Nothing in the header needs a vendor SDK: device handles are `uintptr_t` / `int32_t` file descriptors; typed helpers sit in optional `vmafx/device_<backend>.h` headers.

### 2.2 Objects

| Object | Handle | Created by | Released by | Purpose |
| --- | --- | --- | --- | --- |
| Context | `VmafxContext *` | `vmafx_context_create(const VmafxContextConfig *, VmafxContext **, VmafxError **)` | `vmafx_context_destroy(VmafxContext *, VmafxError **)` (retry-safe) | One scoring session: registered features and models, feature collector, thread pool, per-context log callback |
| Device | `VmafxDevice *` | `vmafx_device_create(const VmafxDeviceDesc *, ...)`: backend + index, or backend + external handles (CUDA context and stream, SYCL queue, HIP device and stream, Metal device and command queue) | `vmafx_device_unref` | Replaces the four `vmaf_*_state_init` / `*_import_state` / `*_state_free` families; shareable across contexts (refcounted) |
| Model | `VmafxModel *`, `VmafxModelSet *` | `vmafx_model_load(cfg, version)`, `vmafx_model_load_file(cfg, path)`, `vmafx_model_set_load*` | `vmafx_model_unref`, `vmafx_model_set_unref` | Refcounted and immutable after load; a context that uses a model holds a reference (ADR-1755 made the collector own mounted models; the new API states it in the type) |
| Options | `VmafxOptions *` | `vmafx_options_set(VmafxOptions **, key, value, err)` | `vmafx_options_free` | Key/value feature options (today `VmafFeatureDictionary`) |
| Frame | `VmafxFrame *` | `vmafx_frame_create_host(device, const VmafxFrameDesc *)`, `vmafx_frame_import(device, const VmafxFrameImport *)`, `vmafx_frame_pool_acquire(pool)` | `vmafx_frame_unref` | Host or device pixels plus an acquire fence; refcounted because temporal extractors keep frames n-1 and n-2 |
| Frame pool | `VmafxFramePool *` | `vmafx_frame_pool_create(device, desc, count)` | `vmafx_frame_pool_destroy` | Replaces `vmaf_preallocate_pictures` and its CUDA / SYCL forms |
| Window | `VmafxWindow *` | `vmafx_window_submit(ctx, const VmafxWindowRequest *)` | `vmafx_window_release` | An asynchronous pooled-score request |
| Error | `VmafxError *` | the library, on failure only | `vmafx_error_free` | Status, message, named subject, cause chain |
| Plain records | `VmafxScore`, `VmafxWindowResult`, `VmafxProvenance`, `VmafxFeatureProvenance`, `VmafxExtractorInfo`, `VmafxFence` | caller-allocated, size-prefixed | n/a | Outputs and descriptors |

Scoring flow:

```c
VmafxContextConfig cfg = VMAFX_CONTEXT_CONFIG_INIT;      /* sets struct_size */
VmafxContext *ctx; VmafxError *err = NULL;
if (vmafx_context_create(&cfg, &ctx, &err)) { log(vmafx_error_message(err)); vmafx_error_free(err); }
vmafx_model_load(&mcfg, "vmaf_v1.0.16_3d0h", &model, &err);
vmafx_context_use_model(ctx, model, &err);   /* ctx takes a reference */
vmafx_model_unref(model);                    /* caller may drop its own at once */
for (i = 0; i < n; i++) {
    vmafx_frame_import(dev, &ref_desc, &ref, &err);   /* or vmafx_frame_create_host */
    vmafx_frame_import(dev, &dist_desc, &dist, &err);
    vmafx_submit(ctx, ref, dist, i, &err);            /* consumes both references */
}
vmafx_flush(ctx, &err);
VmafxScore s = VMAFX_SCORE_INIT;
vmafx_score_pooled(ctx, &(VmafxScoreTarget){.model = model}, VMAFX_POOL_MEAN, 0, n - 1, &s, &err);
/* s.provenance -> version, build, model hash, per-feature backend */
vmafx_context_destroy(ctx, &err);
```

### 2.3 Lifetimes and ownership

- **Context**: `vmafx_context_destroy()` keeps today's retry contract (ADR-1336): a nonzero status leaves the context valid and every dependency (models, devices) retained so the caller can retry; success releases everything the context holds.
- **Models**: refcounted. `vmafx_context_use_model()` takes a reference that lives until the context is destroyed (ADR-1755). Models are immutable after load, so they are shared across contexts and threads without locks.
- **Devices**: refcounted. A context attached to a device holds a reference. External handles (a caller's CUDA context, SYCL queue, Metal command queue) stay owned by the caller and must outlive every device created from them; the library creates its own streams / queues on them.
- **Frames**: refcounted. `vmafx_submit()` consumes the caller's two references. The library keeps a frame while any extractor still reads it: temporal extractors read frames n-1 and n-2 (ADR-1478), so a frame can be retained across up to two further submissions. `vmafx_context_frame_retention(ctx)` returns that depth for the registered extractors so a producer can size its pool (an FFmpeg filter sets `extra_hw_frames` from it). An imported frame never frees producer memory.
- **Fences**: an acquire fence is borrowed for the duration of `vmafx_frame_import()` (the library enqueues a device-side wait on it, or duplicates the file descriptor). A release fence returned by `vmafx_frame_release_fence()` is owned by the caller (event handle or file descriptor to destroy / close).
- **Windows**: a window handle owns its result until `vmafx_window_release()`. The completion callback receives a borrowed pointer valid during the call.
- **Errors**: owned by the caller, one allocation per failure, never on the success path.
- **Strings**: every `const char *` the library returns lives as long as the object it came from (version strings: process lifetime; provenance strings: context lifetime).

### 2.4 Threading

| Object | Rule |
| --- | --- |
| Context | Externally synchronised: calls on one context must not overlap, except `vmafx_window_poll`, `vmafx_window_wait`, `vmafx_window_release`, `vmafx_context_provenance` and score reads of finished frames, which are thread-safe. The context runs extractors on its own thread pool (`n_threads`) |
| Model, device | Thread-safe after creation (atomic refcount; devices serialise their own queues) |
| Frame | May be created on one thread and submitted on another; not shared across contexts |
| Callbacks | Window completion and log callbacks run on library threads and must not call `vmafx_submit` / `vmafx_flush` on the same context |
| Process-global state | None new. The global log level that `vmaf_init` sets today becomes a per-context log callback; the compat layer keeps the global for `libvmaf.h` users |

### 2.5 Error model

```c
typedef int32_t VmafxStatus;     /* 0 ok; >0 informational; <0 error */
#define VMAFX_OK            0
#define VMAFX_PENDING       1    /* window / frame not final yet (today's -EAGAIN) */
#define VMAFX_E_INVALID    -1    /* argument or state; error names the parameter */
#define VMAFX_E_NOMEM      -2
#define VMAFX_E_NOTSUP     -3    /* backend / format / option unsupported; error names it */
#define VMAFX_E_NOTFOUND   -4    /* model version, feature, device index */
#define VMAFX_E_BUSY       -5    /* already imported / attached */
#define VMAFX_E_DEVICE     -6    /* backend runtime failure; error carries the runtime's code */
#define VMAFX_E_IO         -7
#define VMAFX_E_RANGE      -8
#define VMAFX_E_NONFINITE  -9
#define VMAFX_E_TIMEOUT   -10    /* fence wait */
#define VMAFX_E_ABI       -11    /* struct_size smaller than the minimum of this ABI */
```

- Stable numbers on every platform; errno values differ between C runtimes (Windows' `errno.h` numbers several POSIX codes differently), so the compat layer maps status to errno once (`vmafx_status_to_errno()`, generated) and bindings map status to their idiom once (generated from the same table: Rust `enum Error`, Go sentinel errors wrapping `os.ErrInvalid` / `os.ErrNotExist` as `pkg/libvmaf` does today, Python exception classes).
- `VmafxError` carries: `status`, `message` (formatted at the failure site), `subject_kind` (`feature`, `option`, `extractor`, `backend`, `device`, `model`, `frame`, `plane`, `fence`, `path`), `subject` (the name), `function`, and `cause` (another `VmafxError`, e.g. the backend runtime's code). Accessors: `vmafx_error_status/message/subject/subject_kind/cause`, `vmafx_error_free`.
- Passing `NULL` for `error` is allowed; the status alone is returned and the message goes to the context's log callback at `ERROR`, so no failure is silent.
- Out-of-band "last error" (thread-local) was rejected: it breaks with asynchronous completions and across language runtimes that move work between threads.

### 2.6 Versioning and ABI policy

- `vmafx/version.h` (generated): `VMAFX_ABI_VERSION_MAJOR/MINOR/PATCH`; `vmafx_abi_version()` returns the runtime value; `VMAFX_ABI_CHECK()` in the header lets a consumer refuse a runtime with a smaller minor.
- The library's ABI number line is independent of the product version (ADR-1151 / ADR-1235 already keep the `libvmaf` SONAME independent). Proposed: `libvmafx.so.1`, ABI 1.0.0 frozen at the `v1.0.0` tag; until the tag the ABI may change because no release ships it (decision D2).
- Append-only within a major (HISS-14): new functions; new fields at the end of size-prefixed structs; new enum constants with new values; new status codes. Forbidden: removing or renaming a symbol, changing a parameter, reordering or retyping a field, renumbering a constant. A break needs `!`, a `Migration:` footer and a major bump.
- Size negotiation: the caller sets `struct_size = sizeof(T)` (the `*_INIT` macros do). For inputs the library reads the fields it knows up to `struct_size` and treats missing tail fields as their documented defaults; a `struct_size` below the ABI 1.0 minimum is `VMAFX_E_ABI`. For outputs the library writes `min(struct_size, sizeof)` bytes and stores the written size back.
- Symbol versions: a generated linker version script gives every symbol the node of the ABI minor that introduced it (`VMAFX_1.0`, `VMAFX_1.1`, ...) on ELF targets, and a generated `.def` export list on Windows. Both come from `since` in the API definition, so a new symbol cannot ship unversioned.
- Deprecation: `deprecated = { since, replacement, removal }` in the definition generates the `VMAFX_DEPRECATED("use X")` attribute, the docs note and the changelog fragment.

### 2.7 Device frames and fences

```c
typedef struct VmafxFence {
    uint32_t struct_size;
    uint32_t kind;        /* VmafxFenceKind */
    uintptr_t handle;     /* event / shared-event object, 0 when unused */
    uint64_t value;       /* timeline value for shared events, 0 otherwise */
    int32_t fd;           /* sync_file descriptor, -1 when unused */
    uint32_t reserved;    /* 0 */
} VmafxFence;

typedef struct VmafxFrameImport {
    uint32_t struct_size;
    uint32_t memory;      /* VmafxMemoryKind: DEVICE_POINTER, DEVICE_ARRAY, DMABUF, METAL_SURFACE, METAL_TEXTURE, WIN32_SHARED */
    uint32_t pix_fmt;     /* YUV420P/422P/444P/400P, NV12, P010, P016 */
    uint32_t bpc, w, h, n_planes;
    struct { uintptr_t handle; int32_t fd; uint32_t plane_index; uint64_t offset; uint64_t pitch; uint64_t modifier; } plane[3];
    VmafxFence acquire;   /* producer's write -> our read */
    uint32_t flags;       /* VMAFX_IMPORT_REQUIRE_ZERO_COPY (default on) */
} VmafxFrameImport;
```

| Backend | Memory the producer hands over | Acquire fence (producer write before our read) | Release fence (our read before producer reuse) | Notes |
| --- | --- | --- | --- | --- |
| CPU | Host planes (`vmafx_frame_create_host` fills; `vmafx_frame_wrap_host` borrows) | `NONE` (data written before the call) | `HOST` fence: `vmafx_fence_wait()` or the release callback | Borrowed host frames remove today's copy in `vmaf_read_pictures` users that already hold planes |
| CUDA | `CUdeviceptr` per plane (or a CUDA array) in the device's context | `CUDA_EVENT`: an event recorded on the producer's stream; the library enqueues a stream wait on its own stream (no host stall) | `CUDA_EVENT` recorded after the last kernel that reads the frame; the producer waits on its stream before reuse | Replaces the per-frame context barrier of ADR-1199 for callers that pass fences; the barrier stays for compat callers that do not (`VMAF_CUDA_PICTURE_PREALLOCATION_METHOD_DEVICE`) |
| SYCL | USM device pointer, or a Linux dma-buf (fd + format modifier; tiled layouts de-tiled on the device as ADR-1121 does) | `SYCL_EVENT` (pointer to an event of the same context; the library submits a barrier that depends on it) or `SYNC_FILE` exported from the dma-buf | `SYNC_FILE` attached back to the dma-buf (implicit sync for other consumers) or `SYCL_EVENT` | Chroma imported too (#2075); a Windows shared texture path for the RC4 exit (`WIN32_SHARED` + keyed fence) |
| HIP | Device pointer, or a dma-buf imported as external memory | `HIP_EVENT` (stream wait) or `SYNC_FILE` | `HIP_EVENT` or `SYNC_FILE` | No import path exists today; FFmpeg has no HIP hardware device type, so the filter reaches HIP through dma-buf frames |
| Metal | A shared surface plane or a Metal texture | `METAL_SHARED_EVENT` + value (the library encodes a wait), or `NONE` for decoder output that is complete on delivery (covered by a test) | `METAL_SHARED_EVENT` signalled with value + 1 after the last read | Binds the surface instead of the lock + CPU copy of ADR-0423 |

Rules:

- `vmafx_frame_import()` never copies pixel data to the host. With `VMAFX_IMPORT_REQUIRE_ZERO_COPY` (default) a layout the backend cannot bind is refused with `VMAFX_E_NOTSUP` naming the memory kind, pixel format, modifier and plane. A caller that wants a copy says so (`vmafx_frame_create_host` + upload, or the flag cleared, which is logged once per context).
- NV12 / P010 / P016 are converted to planar on the device: a de-interleave and, for P010, the shift of ADR-1679; no arithmetic beyond that, so an imported frame scores bit-identically to the same frame uploaded from the host (ADR-1829 exit evidence).
- Admission: before a frame is counted the context checks every registered extractor against the frame's residency and refuses with each refusing extractor named (ADR-1688 generalised to every backend).
- `sync_file` export / import uses the dma-buf ioctls of the Linux uAPI (`DMA_BUF_IOCTL_EXPORT_SYNC_FILE` / `IMPORT_SYNC_FILE`, present in `include/uapi/linux/dma-buf.h`); the minimum kernel version is to be pinned in WP3 (not verified here).
- Fence tests are failing-first: a test build that skips the acquire wait must produce a wrong score under concurrent device load (the ADR-1199 reproduction harness, `scripts/test/repro-cuda-ffmpeg-nondeterminism.sh`, is the model).

### 2.8 Window scores (synchronous and asynchronous)

```c
typedef struct VmafxWindowRequest {
    uint32_t struct_size;
    uint32_t target_kind;            /* MODEL, MODEL_SET, FEATURE */
    const VmafxModel *model; const VmafxModelSet *model_set; const char *feature;
    uint64_t first, last;            /* inclusive frame indices, as vmaf_score_pooled */
    uint32_t pool_mask;              /* bits of VmafxPool: MIN MAX MEAN HARMONIC_MEAN MEDIAN PERC5 PERC10 PERC20 */
    void (*on_complete)(VmafxWindow *, const VmafxWindowResult *, void *user);  /* optional */
    void *user;
} VmafxWindowRequest;
```

- `vmafx_window_submit()` returns at once. A window completes when every frame in `[first, last]` has its final scores: for `motion2` / `motion3` that is after frame `last + 1` arrived or the context was flushed, and device results are collected one frame late (ADR-1685). Completion either calls `on_complete` on a library thread or is observed with `vmafx_window_poll()` (returns `VMAFX_PENDING` until done) / `vmafx_window_wait(timeout)`.
- The result holds one value per requested pool method plus `n_frames`, `n_scored` (subsampling) and the provenance pointer. It is computed by the same pooling code as the synchronous `vmafx_score_pooled()`, so both are bit-identical; a test asserts it.
- `vmafx_score_pooled()` is the synchronous form and returns `VMAFX_PENDING` where `vmaf_score_pooled()` returns `-EAGAIN` today.

### 2.9 Provenance (#2142)

`VmafxProvenance` (context level, `vmafx_context_provenance()`):

| Field | Source |
| --- | --- |
| `version` | `git describe` of the build (`VMAF_VERSION` today) |
| `abi_major/minor/patch` | `vmafx/version.h` |
| `build_id` | digest of the build configuration: compiler id and version, enabled backends, strict-FP policy (ADR-1461), Rust twins on/off, SIMD level actually dispatched after `cpumask` |
| `models[]` | name, version string, SHA-256 of the model bytes as loaded, flags, feature overrides |
| `features[]` (`VmafxFeatureProvenance`, also per feature through `vmafx_feature_provenance()`) | feature name, extractor name, implementation (`c` / `rust`), backend, device name and runtime version, options string, exactness class from `scripts/ci/exact_twins.d/*` and `LIBM_TWINS` (generated into a table: `exact`, `libm-bounded <bound>`, `tolerance <bound>`, `cpu-reference`), source (`extractor` / `imported`) |

Every `VmafxScore` and `VmafxWindowResult` points at the record. `vmafx_report_write()` embeds it in JSON (`"provenance"` object) and XML (`<provenance>`); CSV and SUB keep their columns and get a `<path>.provenance.json` sidecar when asked (decision in WP5). The CLI's `amend_cli_backend_receipt()` splice disappears because the library writes the same fields; `backend_used` / `feature_backends` stay as aliases (HISS-14). `vmaf --verify-provenance <report>` re-runs and compares bit for bit (#2142).

Prototype limitation: a feature is mapped to its extractor through the extractors' `provided_features`; option-decorated names resolve once the feature collector records the producer of each feature vector (WP5).

### 2.10 Header layout (all generated)

| Header | Content |
| --- | --- |
| `vmafx/vmafx.h` | Umbrella; includes the rest |
| `vmafx/version.h` | ABI version macros, `vmafx_version_string`, `vmafx_abi_version` |
| `vmafx/types.h` | `VmafxStatus`, enums (`VmafxBackend`, `VmafxPixelFormat`, `VmafxPool`, `VmafxLogLevel`, `VmafxFenceKind`, `VmafxMemoryKind`), `VmafxFence`, `*_INIT` macros |
| `vmafx/error.h` | `VmafxError` accessors |
| `vmafx/context.h` | Context, options, feature / model registration, submit, flush, extractor introspection |
| `vmafx/device.h` | Devices, enumeration, profiling |
| `vmafx/frame.h` | Frames, import, pools, fences, side data |
| `vmafx/model.h` | Models and model sets |
| `vmafx/score.h` | Frame scores, pooled scores, windows |
| `vmafx/provenance.h` | Provenance records |
| `vmafx/report.h` | Report writer |
| `vmafx/dnn.h`, `vmafx/mcp.h` | Tiny-AI and embedded MCP server (renamed from today's) |
| `vmafx/device_cuda.h`, `_sycl.h`, `_hip.h`, `_metal.h` | Optional typed helpers that include the backend SDK header (never included by the umbrella) |

### 2.11 Compatibility layer

Structure:

- `libvmaf/*.h` stay byte-for-byte compatible: same declarations, layouts and constant values (a generated layout test pins every public struct of `libvmaf.h` too).
- Three shim kinds. **shim**: generated one-to-one call with argument conversion. **glue**: generated from a declared field map in the API definition (struct conversions such as `VmafConfiguration` to `VmafxContextConfig`). **manual**: hand-written in `core/src/compat/` because the old function's semantics are stateful (picture ownership transfer, the SYCL double-buffer path); the API definition still declares it so the symbol list, docs and conformance cases are generated.
- The compat code calls only exported `vmafx_` functions. With the separate-library packaging (D3) this is a link-time fact: if a `libvmaf` function cannot be written on the public new API, the new API is incomplete.
- `VmafContext *` given to compat users is the engine handle bound to a `VmafxContext` (the prototype keeps an owner pointer in the engine context); `vmafx_context_from_libvmaf()` lets a migrating caller mix both APIs on one session.
- Deprecation: `VMAF_DEPRECATED` on every `libvmaf.h` declaration behind `VMAF_ENABLE_DEPRECATION_WARNINGS` in 1.0 (upstream FFmpeg must build without new warnings), on by default in 1.1, removal in 2.0 (decision D7).
- Conformance: a generated case per compat function runs the old and the new call on the same input and compares results bit for bit (`%a`), plus the `NULL` / invalid-argument cases; the golden-data gate runs through compat automatically because the CLI and the Python harness use `libvmaf.h`.
- Upstream FFmpeg (`n9.0.2`, no patches) needs `pkg-config libvmaf >= 2.0.0`, `libvmaf/libvmaf.h`, `libvmaf/libvmaf_cuda.h` and 18 symbols (`vmaf_init`, `vmaf_close`, `vmaf_use_feature`, `vmaf_use_features_from_model`, `vmaf_feature_dictionary_set`, `vmaf_read_pictures`, `vmaf_picture_alloc`, `vmaf_picture_unref`, `vmaf_score_pooled`, `vmaf_write_output`, `vmaf_model_load`, `vmaf_model_load_from_path`, `vmaf_model_feature_overload`, `vmaf_model_destroy`, `vmaf_cuda_state_init`, `vmaf_cuda_import_state`, `vmaf_cuda_preallocate_pictures`, `vmaf_cuda_fetch_preallocated_picture`; counted in `libavfilter/vf_libvmaf.c` of `n9.0.1`). A CI job builds that FFmpeg against the installed VMAFx and scores three frames (RC4 exit evidence).

Every exported `libvmaf` function (107 at `9bc68a108`: the 104 of `8eabe7c56` plus the three picture-conversion functions of #2140; 49 shim, 31 glue, 27 manual):

| Header | libvmaf function | VMAFx call | Shim kind | Note |
| --- | --- | --- | --- | --- |
| `dnn.h` | `vmaf_dnn_available` | `vmafx_dnn_available` | shim |  |
| `dnn.h` | `vmaf_use_tiny_model` | `vmafx_context_use_tiny_model` | glue | VmafDnnConfig -> VmafxDnnConfig |
| `dnn.h` | `vmaf_dnn_set_codec_context` | `vmafx_context_set_codec_context` | shim |  |
| `dnn.h` | `vmaf_dnn_is_codec_aware` | `vmafx_context_is_codec_aware` | shim |  |
| `dnn.h` | `vmaf_dnn_set_resize_mode` | `vmafx_context_set_tiny_resize` | shim |  |
| `dnn.h` | `vmaf_dnn_session_open` | `vmafx_dnn_session_open` | glue |  |
| `dnn.h` | `vmaf_dnn_session_run_luma8` | `vmafx_dnn_session_run_luma8` | shim |  |
| `dnn.h` | `vmaf_dnn_session_run_plane16` | `vmafx_dnn_session_run_plane16` | shim |  |
| `dnn.h` | `vmaf_dnn_session_run` | `vmafx_dnn_session_run` | shim | VmafDnnInput/Output layouts identical |
| `dnn.h` | `vmaf_dnn_session_close` | `vmafx_dnn_session_close` | shim |  |
| `dnn.h` | `vmaf_dnn_session_attached_ep` | `vmafx_dnn_session_runtime_device` | shim |  |
| `dnn.h` | `vmaf_dnn_verify_signature` | `vmafx_dnn_verify_signature` | shim |  |
| `feature.h` | `vmaf_feature_dictionary_set` | `vmafx_options_set` | shim | VmafFeatureDictionary is VmafxOptions under the old name |
| `feature.h` | `vmaf_feature_dictionary_free` | `vmafx_options_free` | shim |  |
| `libvmaf.h` | `vmaf_init` | `vmafx_context_create` | glue | VmafConfiguration fields -> VmafxContextConfig; returns the engine handle bound to the new context |
| `libvmaf.h` | `vmaf_use_features_from_model` | `vmafx_context_use_model` | shim | model handle bridged; context takes a model reference (ADR-1755) |
| `libvmaf.h` | `vmaf_use_features_from_model_collection` | `vmafx_context_use_model_set` | shim |  |
| `libvmaf.h` | `vmaf_use_feature` | `vmafx_context_use_feature` | shim | VmafFeatureDictionary bridged to VmafxOptions |
| `libvmaf.h` | `vmaf_import_feature_score` | `vmafx_context_import_score` | shim | score source recorded as imported in provenance |
| `libvmaf.h` | `vmaf_read_pictures` | `vmafx_frame_from_picture + vmafx_submit; (NULL, NULL) -> vmafx_flush` | manual | ownership transfer of both pictures kept; picture->frame wrap without copy |
| `libvmaf.h` | `vmaf_score_at_index` | `vmafx_score_frame` | shim | out value = VmafxScore.value |
| `libvmaf.h` | `vmaf_score_at_index_model_collection` | `vmafx_score_frame_model_set` | glue | VmafxModelSetScore -> VmafModelCollectionScore |
| `libvmaf.h` | `vmaf_feature_score_at_index` | `vmafx_feature_score` | shim | prototype slice |
| `libvmaf.h` | `vmaf_score_pooled` | `vmafx_score_pooled` | glue | pool enum mapped; VMAFX_E_PENDING -> -EAGAIN |
| `libvmaf.h` | `vmaf_score_pooled_model_collection` | `vmafx_score_pooled_model_set` | glue |  |
| `libvmaf.h` | `vmaf_feature_score_pooled` | `vmafx_feature_score_pooled` | glue |  |
| `libvmaf.h` | `vmaf_preallocate_pictures` | `vmafx_frame_pool_create (CPU device)` | manual | pool owned by the bridged context |
| `libvmaf.h` | `vmaf_fetch_preallocated_picture` | `vmafx_frame_pool_acquire + vmafx_frame_to_picture` | manual |  |
| `libvmaf.h` | `vmaf_close` | `vmafx_context_destroy` | glue | retry-safe: a nonzero status keeps the context valid (ADR-1336) |
| `libvmaf.h` | `vmaf_write_output` | `vmafx_report_write` | glue | score_format NULL; provenance block added (additive) |
| `libvmaf.h` | `vmaf_write_output_with_format` | `vmafx_report_write` | glue |  |
| `libvmaf.h` | `vmaf_context_get_backend` | `vmafx_context_backend` | shim | VmafxBackend values equal VmafBackend values |
| `libvmaf.h` | `vmaf_feature_backend_twin` | `vmafx_feature_resolve` | glue | VmafxFeatureResolution -> (twin_name, unsupported_option) |
| `libvmaf.h` | `vmaf_registered_feature_extractor` | `vmafx_context_extractor_info` | glue |  |
| `libvmaf.h` | `vmaf_version` | `vmafx_version_string` | shim | prototype slice |
| `libvmaf_cuda.h` | `vmaf_cuda_state_init` | `vmafx_device_create (CUDA, external context)` | glue |  |
| `libvmaf_cuda.h` | `vmaf_cuda_state_free` | `vmafx_device_unref` | shim | retry-safe as today |
| `libvmaf_cuda.h` | `vmaf_cuda_import_state` | `vmafx_context_attach_device` | shim |  |
| `libvmaf_cuda.h` | `vmaf_cuda_preallocate_pictures` | `vmafx_frame_pool_create (CUDA device)` | manual | DEVICE method keeps the ADR-1199 barrier until callers pass fences |
| `libvmaf_cuda.h` | `vmaf_cuda_fetch_preallocated_picture` | `vmafx_frame_pool_acquire + vmafx_frame_to_picture` | manual |  |
| `libvmaf_hip.h` | `vmaf_hip_available` | `vmafx_backend_available(VMAFX_BACKEND_HIP)` | shim |  |
| `libvmaf_hip.h` | `vmaf_hip_state_init` | `vmafx_device_create (HIP, by index)` | glue |  |
| `libvmaf_hip.h` | `vmaf_hip_import_state` | `vmafx_context_attach_device` | shim |  |
| `libvmaf_hip.h` | `vmaf_hip_state_free` | `vmafx_device_unref` | shim |  |
| `libvmaf_hip.h` | `vmaf_hip_list_devices` | `vmafx_device_count + vmafx_device_info (logged)` | glue |  |
| `libvmaf_mcp.h` | `vmaf_mcp_available` | `vmafx_mcp_available` | shim |  |
| `libvmaf_mcp.h` | `vmaf_mcp_transport_available` | `vmafx_mcp_transport_available` | shim |  |
| `libvmaf_mcp.h` | `vmaf_mcp_init` | `vmafx_mcp_server_create` | glue |  |
| `libvmaf_mcp.h` | `vmaf_mcp_start_sse` | `vmafx_mcp_start_sse` | shim |  |
| `libvmaf_mcp.h` | `vmaf_mcp_start_uds` | `vmafx_mcp_start_uds` | shim |  |
| `libvmaf_mcp.h` | `vmaf_mcp_start_stdio` | `vmafx_mcp_start_stdio` | shim |  |
| `libvmaf_mcp.h` | `vmaf_mcp_stop` | `vmafx_mcp_stop` | shim |  |
| `libvmaf_mcp.h` | `vmaf_mcp_close` | `vmafx_mcp_server_destroy` | shim |  |
| `libvmaf_metal.h` | `vmaf_metal_available` | `vmafx_backend_available(VMAFX_BACKEND_METAL)` | shim |  |
| `libvmaf_metal.h` | `vmaf_metal_state_init` | `vmafx_device_create (Metal, by index)` | glue |  |
| `libvmaf_metal.h` | `vmaf_metal_import_state` | `vmafx_context_attach_device` | shim |  |
| `libvmaf_metal.h` | `vmaf_metal_state_free` | `vmafx_device_unref` | shim |  |
| `libvmaf_metal.h` | `vmaf_metal_list_devices` | `vmafx_device_count + vmafx_device_info (logged)` | glue |  |
| `libvmaf_metal.h` | `vmaf_metal_state_init_external` | `vmafx_device_create (Metal, external device and queue)` | glue |  |
| `libvmaf_metal.h` | `vmaf_metal_picture_import` | `vmafx_frame_import (shared surface plane)` | manual | ADR-1679 format table kept |
| `libvmaf_metal.h` | `vmaf_metal_wait_compute` | `vmafx_fence_wait (frame release fence)` | manual |  |
| `libvmaf_metal.h` | `vmaf_metal_read_imported_pictures` | `vmafx_submit (imported frames)` | manual |  |
| `libvmaf_sycl.h` | `vmaf_sycl_state_init` | `vmafx_device_create (SYCL, by index)` | glue |  |
| `libvmaf_sycl.h` | `vmaf_sycl_import_state` | `vmafx_context_attach_device` | shim |  |
| `libvmaf_sycl.h` | `vmaf_sycl_preallocate_pictures` | `vmafx_frame_pool_create (SYCL device)` | manual |  |
| `libvmaf_sycl.h` | `vmaf_sycl_picture_fetch` | `vmafx_frame_pool_acquire + vmafx_frame_to_picture` | manual |  |
| `libvmaf_sycl.h` | `vmaf_sycl_init_frame_buffers` | `vmafx_frame_pool_create (SYCL device, 2 frames)` | manual | shared-buffer zero-copy path of ADR-1121 |
| `libvmaf_sycl.h` | `vmaf_sycl_get_frame_buffers` | `vmafx_frame_device_pointer` | manual |  |
| `libvmaf_sycl.h` | `vmaf_sycl_wait_compute` | `vmafx_fence_wait (frame release fence)` | manual |  |
| `libvmaf_sycl.h` | `vmaf_read_pictures_sycl` | `vmafx_submit (imported frames)` | manual | luma-only admission of ADR-1688 kept |
| `libvmaf_sycl.h` | `vmaf_flush_sycl` | `vmafx_flush` | shim |  |
| `libvmaf_sycl.h` | `vmaf_sycl_dmabuf_import` | `vmafx_frame_import (dma-buf)` | manual |  |
| `libvmaf_sycl.h` | `vmaf_sycl_dmabuf_free` | `vmafx_frame_unref` | manual |  |
| `libvmaf_sycl.h` | `vmaf_sycl_import_va_surface` | `vmafx_frame_import (dma-buf exported from the surface)` | manual |  |
| `libvmaf_sycl.h` | `vmaf_sycl_upload_plane` | `vmafx_frame_upload_plane` | manual |  |
| `libvmaf_sycl.h` | `vmaf_sycl_import_d3d11_surface` | `vmafx_frame_import (Windows shared texture)` | manual |  |
| `libvmaf_sycl.h` | `vmaf_sycl_profiling_enable` | `vmafx_device_set_profiling(true)` | shim |  |
| `libvmaf_sycl.h` | `vmaf_sycl_profiling_disable` | `vmafx_device_set_profiling(false)` | shim |  |
| `libvmaf_sycl.h` | `vmaf_sycl_profiling_print` | `vmafx_device_profile_report + log` | glue |  |
| `libvmaf_sycl.h` | `vmaf_sycl_profiling_get_string` | `vmafx_device_profile_report` | glue |  |
| `libvmaf_sycl.h` | `vmaf_sycl_state_free` | `vmafx_device_unref` | shim |  |
| `libvmaf_sycl.h` | `vmaf_sycl_list_devices` | `vmafx_device_count + vmafx_device_info (logged)` | glue |  |
| `model.h` | `vmaf_model_load` | `vmafx_model_load` | glue | VmafModelConfig -> VmafxModelConfig |
| `model.h` | `vmaf_model_load_from_path` | `vmafx_model_load_file` | glue |  |
| `model.h` | `vmaf_model_feature_overload` | `vmafx_model_override_feature` | shim |  |
| `model.h` | `vmaf_model_destroy` | `vmafx_model_unref` | shim | drops the caller reference; mounted models live on (ADR-1755) |
| `model.h` | `vmaf_model_feature_count` | `vmafx_model_feature_count` | shim |  |
| `model.h` | `vmaf_model_feature_name` | `vmafx_model_feature_name` | shim |  |
| `model.h` | `vmaf_model_collection_load` | `vmafx_model_set_load` | glue | returns the first member as VmafModel as today |
| `model.h` | `vmaf_model_collection_load_from_path` | `vmafx_model_set_load_file` | glue |  |
| `model.h` | `vmaf_model_collection_feature_overload` | `vmafx_model_set_override_feature` | glue |  |
| `model.h` | `vmaf_model_collection_destroy` | `vmafx_model_set_unref` | shim |  |
| `model.h` | `vmaf_model_version_next` | `vmafx_model_builtin_next` | shim |  |
| `model.h` | `vmaf_default_model_version` | `vmafx_model_default_version` | shim |  |
| `perceptual_weight.h` | `vmaf_set_perceptual_weight_enabled` | `vmafx_context_set_option("perceptual_weight", ...)` | glue |  |
| `perceptual_weight.h` | `vmaf_set_perceptual_weight_strength` | `vmafx_context_set_option("perceptual_weight_strength", ...)` | glue |  |
| `perceptual_weight.h` | `vmaf_set_perceptual_sidedata` | `vmafx_frame_attach_sidedata (by index on the bridged context)` | manual |  |
| `picture.h` | `vmaf_picture_alloc` | `vmafx_frame_create_host + vmafx_frame_to_picture` | manual | VmafPicture stays a caller-visible struct |
| `picture.h` | `vmaf_picture_unref` | `vmafx_frame_unref (via VmafPicture.ref)` | manual |  |
| `picture.h` | `vmaf_picture_convert_context_init_with_color` | `vmafx_frame_converter_create` | glue | VmafColor + VmafPictureConvertTarget -> VmafxConvertDesc (#2140) |
| `picture.h` | `vmaf_picture_convert` | `vmafx_frame_convert` | manual | VmafPicture in and out bridged to frames |
| `picture.h` | `vmaf_picture_convert_context_close` | `vmafx_frame_converter_destroy` | shim |  |
| `picture_v2.h` | `vmaf_picture2_alloc` | `vmafx_frame_create_host` | manual | v2 struct kept until 2.0 with v1 |
| `picture_v2.h` | `vmaf_picture2_unref` | `vmafx_frame_unref` | manual |  |
| `picture_v2.h` | `vmaf_picture_v1_to_v2` | `compat-internal` | manual | pure struct conversion, no new-API call |
| `picture_v2.h` | `vmaf_picture_v2_to_v1` | `compat-internal` | manual |  |
| `picture_v2.h` | `vmaf_backend_handle_name` | `vmafx_backend_name` | shim | VMAF_BACKEND_HANDLE_* mapped to VmafxBackend |

## 3. Generation: one definition, every surface

### 3.1 Requirements

| # | Requirement |
| --- | --- |
| R1 | Describes a C ABI as it is: opaque handles, size-prefixed structs, fixed-width fields, out-parameters, ownership transfer, optional error out-parameter, callbacks with a user pointer, fences as plain structs |
| R2 | The C implementation stays C (the engine is C; Rust twins are internal, ADR-1713); the public API must exist in builds without Rust |
| R3 | Also describes the non-binding surfaces: option groups shared by CLI, FFmpeg, MCP, gRPC; status table; provenance fields |
| R4 | Generates: C headers, compat shims and glue, Rust FFI + safe layer, Go, Python, proto messages, MCP JSON schemas, CLI option table and usage, FFmpeg AVOption table, docs, ABI layout tests, linker version script / `.def`, changelog drafts |
| R5 | Offline: no network at build or generation time; nothing new at build time if possible |
| R6 | Licence compatible with EUPL-1.2 for the tool and, more importantly, no licence on its output |
| R7 | A breaking change is caught mechanically before merge (HISS-14) |
| R8 | Every generated file passes the repository's own gates (HISS-04 function size, HISS-09 `// SAFETY:`, clang-tidy lanes, ruff, gofmt, rustfmt) without a post-processing patch-up step |

### 3.2 Candidates (versions and licences checked 2026-10-05)

| | (a) In-house IDL (TOML) + Python generators | (b) Rust as source: cbindgen + Diplomat / UniFFI | (c) WIT + wit-bindgen | (d) protobuf as IDL + custom plugins | (e1) Annotated C headers + libclang | (e2) SWIG |
| --- | --- | --- | --- | --- | --- | --- |
| Current version | n/a (repository code) | cbindgen 0.29.4 (2026-06-09); Diplomat 0.16.1 (2026-08-20, needs Rust >= 1.88); UniFFI 0.32.2 (2026-09-23) | wit-bindgen 0.62.0 (2026-09-10) | protobuf v36.2 (2026-09-17), buf v1.73.0 (2026-09-11); host has `libprotoc 36.1`, `buf 1.67.0` | bindgen 0.73.2 (2026-09-08) or `clang -ast-dump=json` | SWIG 4.5.1 |
| Fit for a C ABI with opaque handles and callbacks (R1) | Full: the definition states each property directly | cbindgen emits C/C++ headers for `extern "C"` Rust items only. Diplomat: opaque types are Rust `Box` values, "callbacks in parameters: support is limited"; UniFFI's FFI is an internal detail between its own generated sides, not a public C ABI | None: generates WebAssembly component bindings ("produces WebAssembly components only", guests in Rust, C, C++, C#, Go, MoonBit); CLI declared not stable | Poor: no pointers, handles, out-parameters, callbacks or fixed-size arrays; everything becomes a custom option (an extension of the descriptor option messages) read by our own plugin | Good for C (the header is the truth); ownership and errors need annotation macros | Wrapper generator, not an ABI definition; ownership by typemaps |
| Implementation language constraint (R2) | None | The API's implementation must be Rust (both tools bind Rust code; "focuses exclusively on Rust implementations"); breaks R2 | Wasm only | None | None | None |
| Languages / artefacts covered (R3, R4) | All of R4, each by a small emitter we write | C, C++ (cbindgen); Diplomat: C, C++, Dart, JS/TS, .NET, Kotlin, Python; UniFFI: Kotlin, Swift, Python, Ruby (+ third-party Go, C#, ...). No CLI / FFmpeg / MCP / proto outputs | Wasm guests | Any language through plugins (`CodeGeneratorRequest` on stdin); gRPC and Go stubs for free; C ABI, compat, CLI, FFmpeg, MCP, docs all custom | Bindings via bindgen (Rust) / custom; non-binding surfaces do not live in C headers | Python, Go, C#, Java, ...; not Rust; non-binding surfaces none |
| Build-time needs (R5) | Python >= 3.11 standard library (`tomllib`); Meson already runs Python | Rust toolchain for the core library; cbindgen / Diplomat as developer tools | Wasm toolchain | protoc or buf + a plugin runtime (protobuf Python package for a Python plugin) | libclang at generation time | SWIG at generation time |
| Licence (R6) | Repository licence (EUPL-1.2) | cbindgen MPL-2.0; Diplomat MIT OR Apache-2.0; UniFFI MPL-2.0 (developer tools; output unencumbered) | Apache-2.0 WITH LLVM-exception OR Apache-2.0 OR MIT | protobuf BSD-3-Clause, buf Apache-2.0 | bindgen BSD-3-Clause; LLVM Apache-2.0 WITH LLVM-exception | Tool GPL; "its output is not governed by SWIG's license" |
| Maturity / maintenance | Ours to maintain (estimated 1.5-3 k lines of Python for all emitters) | Active; UniFFI "a long way from a 1.0 release" | Pre-1.0, unstable CLI | Mature, widely used | Mature parser; annotation scheme ours | Mature, old |
| Breaking-change detection (R7) | Our append-only checker on the definition (vs. last tag and merge base) + generated layout tests + linker version script + `check_exported_symbols.py` (+ optional `abidiff`) | cbindgen output diff; no ABI rule engine | n/a | `buf breaking` (FILE / PACKAGE / WIRE_JSON / WIRE, offline against `.git#branch=...`): wire and generated-source breakage, not C layout | Header diff + layout tests; `abidiff` | None |
| Generated code meets repo gates (R8) | We control the templates | cbindgen headers fine; Diplomat / UniFFI glue is theirs to shape | n/a | Go stubs need `postprocess_gen_go.py` today for HISS-09 | Bindings shaped by bindgen | Wrapper style fixed by SWIG |

Sources are listed in section 9.

### 3.3 Recommendation: (a) in-house TOML definition + standard-library Python generators

- It is the only option that satisfies R1-R3 together. (b) would move the public API's implementation into Rust, which RC4 does not do (the engine stays C, Rust twins are internal and optional). (c) does not target native libraries at all. (d) fights its type model for every C construct and still leaves every non-binding emitter to us; it is the right tool for the gRPC surface, which is why the generator emits `.proto` messages that `buf` then compiles and checks. (e1) keeps C developers in C but cannot hold option groups, MCP schemas or FFmpeg tables, and annotation macros in public headers are their own maintenance surface.
- Nothing new at build time: generated files are committed, and regeneration needs only `python3 >= 3.11`. Rust users stop needing libclang and bindgen at build time; Go users stop hand-writing cgo.
- Risks and their answers: (1) emitter code is ours: keep each emitter small and tested, emit only what the repository uses; (2) an in-house IDL can grow into a language: the definition holds data only (no expressions) except compat glue statements, which are C fragments compiled and tested by the conformance cases; (3) `buf breaking` still guards the generated proto.
- TOML over JSON (no comments) and YAML (needs a third-party parser) because the standard library reads it (`tomllib`, Python 3.11+, already used in `scripts/ci/check-workflow-versions.py`).

### 3.4 Shape of the definition

```toml
[api]
name = "vmafx"
abi_version = "1.0.0"
header_dir = "vmafx"

[[status]]
name = "VMAFX_E_NOTSUP"
value = -3
errno = "ENOTSUP"
doc = "Backend, format or option not supported; the error names it."

[[handles]]
name = "VmafxContext"
release = "vmafx_context_destroy"
doc = "One scoring session."

[[structs]]
name = "VmafxProvenance"
sized = true                # first field is struct_size; append-only
since = "1.0"
fields = [
  { name = "version", type = "cstr", doc = "git describe of the build" },
  { name = "abi_major", type = "u32" },
]

[[functions]]
name = "vmafx_context_create"
since = "1.0"
returns = "status"
params = [
  { name = "config", type = "VmafxContextConfig", pass = "in_ptr", nullable = true },
  { name = "out", type = "VmafxContext", pass = "out_handle" },
  { name = "error", type = "VmafxError", pass = "out_error" },
]

[[compat]]
name = "vmaf_feature_score_at_index"
header = "libvmaf/libvmaf.h"
kind = "shim"
target = "vmafx_feature_score"
```

Option groups (one per concept: model, feature, backend, device, threads, subsample, pool, precision, output, window, tiny model, masks) carry `type`, `default`, `range` / `enum`, `doc`, and per-surface spellings: `cli = ["--threads"]`, `ffmpeg = "threads"` (+ `aliases = ["n_threads"]`), `mcp = "threads"`, `proto = { field = 7 }`. The generator refuses a group without a spelling for a surface it is declared on.

### 3.5 Generated artefacts

| Artefact | Path | Emitter | Committed |
| --- | --- | --- | --- |
| C headers | `core/include/vmafx/*.h` | `emit_c` | yes |
| Compat shims and glue | `core/src/compat/*.gen.c` (manual ones in `core/src/compat/*.c`) | `emit_compat` | yes |
| Linker version script, Windows `.def` | `core/src/vmafx.map`, `core/src/vmafx.def` | `emit_symbols` | yes |
| ABI layout tests | `core/test/test_vmafx_abi_layout.c` (+ `layout` constants inside each binding) | `emit_layout` | yes |
| Compat conformance cases | `core/test/test_compat_conformance.gen.c` | `emit_conformance` | yes |
| Rust | `bindings/rust/vmafx-sys/src/ffi.rs` (replaces bindgen), `bindings/rust/vmafx/src/gen/*.rs` (handles with `Drop`, `Result<_, Error>`) | `emit_rust` | yes |
| Go | `pkg/vmafx/zz_generated_*.go` (cgo) | `emit_go` | yes |
| Python | `bindings/python/vmafx/_api.py` (ctypes, standard library only) | `emit_python` | yes |
| gRPC messages | `proto/vmafx/v1/vmafx_api.proto` (messages and enums; services stay hand-written) | `emit_proto` | yes, then `buf generate` |
| OpenAPI components | `api/openapi/components.gen.yaml` | `emit_openapi` | yes |
| MCP schemas | `mcp/schemas/*.json` (JSON Schema 2020-12, the MCP default dialect) read by both servers | `emit_mcp` | yes |
| CLI | `core/tools/cli_options.gen.inc` (long options, usage text, value parsers' table) | `emit_cli` | yes |
| FFmpeg | `ffmpeg-patches/src/vf_vmafx_options.h`, folded into the filter patch by `ffmpeg_patch_stack.py --refresh` | `emit_ffmpeg` | yes |
| Docs | `docs/api/vmafx/*.md` reference, `docs/api/vmafx/compat.md`, option tables in `docs/usage/cli.md` and `docs/usage/ffmpeg.md` between generated markers | `emit_docs` | yes |
| Status / errno / exception maps | inside the C, Rust, Go, Python outputs | shared | yes |
| Changelog draft | `changelog.d/added/api-*.md` when the definition gains or deprecates a symbol | `--changelog` | written by the author |

### 3.6 How a breaking change is caught

1. **Append-only checker** (`python3 -m vmafx_api --abi-check --against <ref>`): loads the definition at the merge base and at the last release tag and refuses removed or renamed symbols, changed parameters, reordered / retyped / removed fields, fields added to an unsized struct, renumbered constants, a lowered `since`. Exception: the PR title carries `!` and the body a `Migration:` footer, and `abi_version` major went up.
2. **Drift check** (Meson test, every build): regenerating into a temporary directory must reproduce the committed files byte for byte. A hand edit of any generated file fails.
3. **Layout tests**: the generator computes `sizeof`, alignment and every offset for the LP64 and LLP64 data models from the definition; the C test asserts them with `_Static_assert` and each binding asserts its own view at import / compile time. The compiler, the definition and the binding must agree.
4. **Symbol checks**: `check_exported_symbols.py` reads the generated symbol list; the version script makes a symbol without a `since` node a link error.
5. **Optional `abidiff`** (libabigail, Apache-2.0 WITH LLVM-exception) between the last release's `.so` and the candidate in the release workflow, as an independent second opinion.
6. **`buf breaking`** on the generated proto.

## 4. What gets automated beyond bindings

| Item | Today | With the definition |
| --- | --- | --- |
| Scoring options | CLI table, CLI usage, Go MCP schema, Python MCP schema, proto, OpenAPI, four FFmpeg tables, docs: all by hand, parity tests detect drift | One option group per concept; every surface generated; the parity tests become a drift check of generated files (the Go literal copy of the Python tool list goes away) |
| MCP argv building | Both servers build `vmaf` argv by hand (ADR-1117) | Generated argv spec (option -> flag) shared by both servers |
| ABI check in CI | none | Append-only checker + layout tests + version script + optional `abidiff` |
| Compat conformance | none | One generated case per compat function; golden gate through compat |
| Symbol export check | Regex over headers | Generated symbol list (exact) |
| FFmpeg surface check | Grep of patches (`ffmpeg-patches-surface-check.sh`) | The definition records which symbols and options the filter consumes |
| Changelog | Hand fragment | Draft fragment per added / deprecated symbol |
| Deprecations | Comments | Attribute + docs + changelog + optional runtime warning once per symbol, from one entry |
| Docs | Signatures restated by hand | Reference pages generated; prose pages link to them |
| Provenance exactness table | Prose in ADRs, fragments in `scripts/ci/exact_twins.d/` | Generated C table from the fragments, reported in every score |
| Status and error mapping | Per binding by hand (Rust 5 values, Go 4 sentinels) | One table, every binding |
| Backend selection | Python `score_backend.py` and its Go port | Library call `vmafx_device_count/info`, exposed in every binding; both ports retire (HISS-19) |

## 5. FFmpeg `vmafx` filter

### 5.1 Shape

- Two video inputs like upstream `libvmaf`: `main` (distorted) and `reference`, synchronised with `framesync` (its `eof_action`, `shortest`, `repeatlast`, `ts_sync_mode` options come along). Output: the `main` frames, unchanged.
- Built on `vmafx/*.h` only; configure `--enable-libvmafx`, `require_pkg_config libvmafx "libvmafx >= 1.0.0" vmafx/vmafx.h vmafx_context_create`.
- One filter for every backend; the backend follows the input frames (`backend=auto`).

### 5.2 Options (generated from the option groups)

| Option | Type | Default | Meaning |
| --- | --- | --- | --- |
| `model` | string, `\|`-separated | library default (`version=vmaf_v1.0.16_3d0h`) | Model specs as today (`version=`, `path=`, `name=`, feature overrides) |
| `feature` | string, `\|`-separated | none | Extra features with options |
| `backend` | enum `auto`, `cpu`, `cuda`, `sycl`, `hip`, `metal` | `auto` | `auto`: hardware frames pick their backend, software frames use the CPU. Any other value is strict: unavailable = error naming it |
| `device` | string | `auto` | Device index or name for software frames on a GPU backend; ignored (and checked) for hardware frames, whose device comes from the frames context |
| `import` | enum `auto`, `device`, `host` | `auto` | `auto`: hardware frames are imported without copy, software frames are uploaded (a GPU backend on software frames always uploads, which is the input's nature, not a fallback). `device`: refuse software frames. `host`: download hardware frames explicitly (logged once with the reason) |
| `threads` (alias `n_threads`) | int | 0 | CPU worker threads |
| `subsample` (alias `n_subsample`) | int >= 1 | 1 | Score every n-th frame |
| `pool` | flags `min`, `max`, `mean`, `harmonic_mean`, `median`, `perc5`, `perc10`, `perc20` | `mean` | Pool methods for the final score and the windows |
| `n_stats` | duration | 0 (off) | Window length in seconds (#2138) |
| `n_stats_frames` | int | 0 (off) | Window length in frames (exclusive with `n_stats`) |
| `stats_out` | flags `log`, `metadata`, `file` | `log` | Where window statistics go |
| `stats_path` | string | none | NDJSON file, one object per window |
| `log_path` | string | none | Full report |
| `log_fmt` | enum `json`, `xml`, `csv`, `sub` | `json` | Report format (`json` and `xml` carry provenance) |
| `score_fmt` | string | `%.6f` | printf format for scores (`%.17g` lossless) |
| `provenance` | flags `log`, `report` | `log+report` | Print the provenance record at init and embed it in the report |
| `metadata` | bool | 0 | Per-frame scores as frame metadata `lavfi.vmafx.<metric>` (adds a delay of the retention depth) |
| `cpumask`, `gpumask` | int64 | 0 | As today |
| `tiny_model`, `tiny_device`, `tiny_threads` | as today | | Tiny-AI |
| `perceptual_weight` | bool | 0 | As today (side data) |
| `profile` | bool | 0 | Device timing breakdown at uninit (today `gpu_profile` / `sycl_profile`) |

### 5.3 Hardware frames per backend

| Backend | Input frames | How the filter imports | Fences |
| --- | --- | --- | --- |
| CPU | Software YUV 4:2:0 / 4:2:2 / 4:4:4 / 4:0:0, 8-16 bit, NV12 / P010 | `vmafx_frame_wrap_host` on the AVFrame planes (no copy); the AVFrame reference is kept until the release fence | `NONE` / host release |
| CUDA | `AV_PIX_FMT_CUDA` (software format NV12, P010, YUV420P, YUV420P10, YUV444P, ...) | Device created once from the frames context's CUDA context and stream; planes are `CUdeviceptr` | Acquire: event recorded on the frames context's stream. Release: the library's event; the filter makes the frames context's stream wait on it before it unrefs the reference frame (no host stall) |
| SYCL | Linux DRM PRIME frames (dma-buf), directly or mapped from VA hardware frames with `hwmap`; Windows shared textures | `vmafx_frame_import(DMABUF)` per object / plane with the format modifier | Acquire: `sync_file` exported from the dma-buf. Release: `sync_file` imported back into the dma-buf |
| HIP | Linux DRM PRIME frames (FFmpeg has no HIP device type) | `vmafx_frame_import(DMABUF)` into HIP external memory | `sync_file` both ways |
| Metal | macOS hardware frames (pixel buffers backed by a shared surface) | `vmafx_frame_import(METAL_SURFACE)` per plane | Acquire `NONE` (decoder output is complete on delivery; covered by a test) or a shared event; release: shared event |

Both inputs must resolve to the same backend and device; otherwise the filter fails at config time naming both inputs' formats and devices (a host bridge is the user's explicit choice: `hwdownload,format=...` or `import=host`).

### 5.4 Import rule: retry, then fail named

Interpretation of the maintainer's "retry-then-fail-named" rule, built on ADR-1688 and ADR-1679 (to be confirmed, D8):

1. An import or admission failure with a transient status (`VMAFX_E_BUSY`, `VMAFX_E_TIMEOUT` on the acquire fence, pool exhaustion) is retried once after a host wait on the acquire fence.
2. A second failure, or any non-transient one (`VMAFX_E_NOTSUP`, `VMAFX_E_INVALID`), fails the filter graph with one error line naming: backend, device, input (`main` / `reference`), pixel format and software format, modifier, plane, and each refusing extractor with the option that makes it refuse.
3. No frame passes through unscored, no score line is printed after a failure (ADR-1688's `VMAF score: 0.000000` fix), and nothing silently falls back to a host copy.

### 5.5 `n_stats` windows (#2138)

- Window `k` holds the frames whose presentation time lies in `[t0 + k*n, t0 + (k+1)*n)` (`t0` = first frame), or frames `[k*m, (k+1)*m)` with `n_stats_frames=m`.
- When the first frame past a window arrives, the filter submits a `VmafxWindowRequest` for the window's indices and the requested pool methods. It completes after the frames' scores are final (one frame later for `motion2` / `motion3`, one frame later again on device backends), so window output trails by a bounded number of frames, never by a window.
- At end of stream the last window is flushed and marked `"partial": true`.
- With `subsample`, statistics cover the scored frames; the record carries `n_frames` and `n_scored`.
- Output per window, one line (`log`), one NDJSON object (`file`) and frame metadata `lavfi.vmafx.window.<metric>.<pool>` on the next output frame (`metadata`):

```json
{"window": 3, "start": 6.000, "end": 8.000, "n_frames": 48, "n_scored": 48, "partial": false,
 "vmaf": {"min": 91.2, "max": 99.8, "mean": 96.4, "harmonic_mean": 96.3}}
```

(Values illustrative.) The final report also lists the windows.

### 5.6 Output

- `log_path` / `log_fmt`: `vmafx_report_write()` with provenance (JSON / XML), the windows and the per-frame scores.
- Log: provenance at init (`version`, `build_id`, model hash, per-feature backend and exactness class), one line per window, the pooled scores at uninit.
- Metadata: per-frame (`metadata=1`) and per-window keys.

### 5.7 Migration from each retired filter option

| Old filter | Old option | `vmafx` equivalent |
| --- | --- | --- |
| `libvmaf`, `libvmaf_cuda`, `libvmaf_sycl`, `libvmaf_metal` | `model` | `model` (same syntax). The `libvmaf` filter's default was `version=vmaf_v0.6.1`; `vmafx` uses the library default, so pass `model=version=vmaf_v0.6.1` to reproduce old numbers |
| same | `feature` | `feature` |
| same | `log_path` | `log_path` |
| same | `log_fmt` | `log_fmt` (default now `json`; `xml` still available) |
| same | `pool` | `pool` (now several methods) |
| same | `n_threads` | `threads` (alias `n_threads` kept) |
| same | `n_subsample` | `subsample` (alias `n_subsample` kept) |
| same (patch 0014) | `cpumask`, `gpumask` | `cpumask`, `gpumask` |
| same (patch 0016) | `score_fmt` | `score_fmt` |
| `libvmaf` (patch 0001) | `tiny_model`, `tiny_device`, `tiny_threads` | same names |
| `libvmaf` (patch 0003) | `sycl_device=N` | `backend=sycl:device=N` (software frames) or SYCL-importable hardware frames |
| `libvmaf` (patch 0003) | `sycl_profile=1` | `profile=1` |
| `libvmaf` (patch 0004) | `vulkan_device` | removed (backend removed, ADR-0726); the filter errors naming it |
| `libvmaf` (patch 0010) | `cuda=1` | `backend=cuda` (software frames are uploaded) or CUDA hardware frames |
| `libvmaf` (patch 0011) | `hip_device=N` | `backend=hip:device=N` |
| `libvmaf` (patch 0012) | `metal_device=-2` / `-1` / `N` | `backend=cpu` / `backend=metal` / `backend=metal:device=N` |
| `libvmaf` (patch 0017) | `perceptual_weight` | `perceptual_weight` |
| `libvmaf` (patch 0018) | `pool=perc5/perc10/perc20` | `pool=perc5` ... |
| `libvmaf_sycl` (patch 0005) | `gpu_profile` | `profile` |
| `libvmaf_sycl` | hardware frames (luma only, ADR-1688) | hardware frames with chroma (#2075) |
| `libvmaf_metal` (patch 0013) | shared-surface frames copied on the CPU (ADR-0423) | bound without copy |
| `libvmaf_cuda` (upstream) | CUDA frames copied into libvmaf's pool | imported without copy |
| `libvmaf_vulkan` (patch 0006) | Vulkan frames | `vmafx` on Vulkan frames mapped to DRM PRIME (`hwmap`), scored on SYCL or HIP (section 5.9) |
| `libvmaf_tune` (patch 0008) | all | `vmafx_tune` (section 5.9) |

### 5.8 Patch series after the switch

| Patch | Fate |
| --- | --- |
| 0001, 0003, 0004, 0010, 0011, 0012, 0014, 0016, 0017, 0018, 0020 (fork changes to upstream `vf_libvmaf.c`) | Removed; their capabilities move into `vmafx` (section 5.9). Upstream's own `libvmaf` / `libvmaf_cuda` code returns to upstream's file; the fork's FFmpeg builds configure `--enable-libvmafx` only, so no filter with a `vmaf` name is built. Unpatched upstream FFmpeg keeps building its `libvmaf` filter against the compat layer (CI job, WP6) |
| 0005 `libvmaf_sycl`, 0006 `libvmaf_vulkan`, 0013 `libvmaf_metal` | Removed; replaced by `vmafx` |
| 0008 `libvmaf_tune` | Replaced by `vmafx_tune` (new patch, VMAFx API) |
| 0002 `vmaf_pre` | Replaced by `vmafx_pre` (renamed filter, same options) |
| 0015 `-vmaf-profile` CLI glue | Renamed option `-vmafx-profile` |
| 0007 (`-qpfile` on three encoders), 0009 (`-pass-autotune`), 0019 (build diagnostics) | Kept; no `vmaf` name; refreshed onto the new series |
| new `00NN-add-vmafx-filter.patch` | Added: `libavfilter/vf_vmafx.c`, generated `vf_vmafx_options.h`, configure / Makefile / allfilters entries, docs in `doc/filters.texi` |

`docs/usage/ffmpeg.md` gains the migration guide (sections 5.7 and 5.9 as prose plus worked commands, including the #2138 encode-and-score command).

### 5.9 Every filter capability under a VMAFx name (D6)

Maintainer decision D6: every capability of today's 20 patches survives, under a VMAFx name; no filter keeps a `vmaf` name; the old names go in the same step.

| Old name (patches) | Capability | New name | Options: old -> new |
| --- | --- | --- | --- |
| `libvmaf` (FFmpeg's filter + 0001, 0003, 0004, 0010, 0011, 0012, 0014, 0016, 0017, 0018, 0020) | Score software frames on CPU, CUDA (upload), SYCL, HIP or Metal; tiny-AI model; percentile pooling; Pelorus perceptual weighting; retry-safe close | `vmafx` | `model`, `feature`, `log_path`, `log_fmt`, `pool` (all methods incl. `perc5`/`perc10`/`perc20`), `cpumask`, `gpumask`, `score_fmt`, `perceptual_weight`, `tiny_model`, `tiny_device`, `tiny_threads`: same names. `n_threads` -> `threads` (alias kept), `n_subsample` -> `subsample` (alias kept). `cuda=1` -> `backend=cuda`. `sycl_device=N` -> `backend=sycl:device=N`. `hip_device=N` -> `backend=hip:device=N`. `metal_device=-2/-1/N` -> `backend=cpu` / `backend=metal` / `backend=metal:device=N`. `sycl_profile=1` -> `profile=1`. `vulkan_device` -> the Vulkan input row below. Retry-safe close: internal (ADR-1336 contract of `vmafx_context_destroy`) |
| `libvmaf_cuda` (FFmpeg's filter) | Score CUDA hardware frames | `vmafx` | Same options as `libvmaf`; `backend=auto` picks CUDA from the frames; imported without the device-to-device copy |
| `libvmaf_sycl` (0005) | Score hardware-decoded frames on SYCL (luma zero-copy today) and software frames | `vmafx` | `log_path`, `log_fmt`, `pool`, `model`, `feature`, `cpumask`, `gpumask`, `score_fmt`: same. `n_threads` / `n_subsample` as above. `gpu_profile` -> `profile`. Frames: DRM PRIME (or VA frames mapped with `hwmap`), luma and chroma (#2075) |
| `libvmaf_metal` (0013) | Score macOS hardware frames (NV12 / P010) | `vmafx` | Same option list as `libvmaf_sycl` without `gpu_profile`; frames bound without the CPU copy |
| `libvmaf_vulkan` (0006) | Vulkan hardware frames (builds no code since the backend's removal, ADR-0726) | `vmafx` | Same option list. Vulkan frames are scored by mapping them to DRM PRIME (`hwmap=derive_device=drm`) and importing the dma-buf on SYCL or HIP; there is no Vulkan compute backend |
| HIP (`hip_device` on `libvmaf`, 0011) | Score on HIP | `vmafx` | `backend=hip[:device=N]`; DRM PRIME frames imported without copy (new: no import path today) |
| `libvmaf_tune` (0008) | Recommend a CRF for the next pass from a scored first pass | `vmafx_tune` | `model`, `feature`, `recommend_target_vmaf`, `recommend_crf_min`, `recommend_crf_max`, `recommend_passes`: same names (`vmaf` there names the metric, not the product). `n_threads` -> `threads` (alias kept). New, shared with `vmafx`: `backend`, `device`. Default model becomes the library default (was `vmaf_v0.6.1`) |
| `vmaf_pre` (0002) | Learned ONNX pre-filter, 8/10-bit, optional chroma | `vmafx_pre` | `model`, `device`, `threads`, `chroma`: same |
| `-vmaf-profile` (0015, fftools) | Hand an encoder-profile report to vmaf-tune | `-vmafx-profile` | Same argument (`path`) |
| `-pass-autotune` (0009, fftools) | 2-pass orchestration hint | unchanged | No `vmaf` name |
| `-qpfile` on three encoders (0007) | Per-frame / per-MB QP offsets from vmaf-tune | unchanged | No `vmaf` name |
| 0019 | Warning-clean GCC 14 / 16 builds | unchanged | Not a filter |

Configure switches: `--enable-libvmafx` builds `vmafx`, `vmafx_tune` and `vmafx_pre` against `libvmafx` (`require_pkg_config libvmafx "libvmafx >= 1.0.0" vmafx/vmafx.h vmafx_context_create`); the fork's patches no longer add `--enable-libvmaf-sycl`, `--enable-libvmaf-metal`, `--enable-libvmaf-vulkan` or `--enable-libvmaf-hip`.

## 6. Plan for RC4

### 6.1 Work packages

| WP | Content | Depends on | Parallel with |
| --- | --- | --- | --- |
| WP0 | ADR-1852 Accepted with D1-D8 (docs PR `docs/adr-vmafx-api-redesign`), this design as Research-2158 | none | done |
| WP1 | Definition + generator core: loader / validator, C header, layout tests, drift check, append-only checker, symbol list, version script (prototype is the seed) | WP0 decisions D1-D3 | |
| WP2 | C implementation of the core API on the engine: context, options, models (refcounted), host frames, submit / flush, frame and feature scores, pooled scores, extractor introspection, errors, per-context log | WP1 | WP7, WP8 |
| WP3 | Device frames and fences (the #1723 import part): CUDA, SYCL (chroma, dma-buf, Windows shared textures), HIP (new), Metal (binding), NV12 / P010 on device, admission | WP2 frame object | one lane per backend (device locks) |
| WP4 | Asynchronous windows (submit / poll / wait / callback), equality with the synchronous call | WP2 | WP3, WP5 |
| WP5 | Provenance in the library (#2142): build id, model hash, collector records producers, exactness table, report embedding, `--verify-provenance` | WP2 | WP3, WP4 |
| WP6 | Compat layer: generated shims and glue, manual compat, library split (D3), conformance cases, golden gate through compat, upstream-FFmpeg build job | WP2 (+ WP3 for GPU compat functions) | WP7 |
| WP7 | Bindings: Rust (`-sys` replacing bindgen, safe layer), Go (`pkg/vmafx`, then `pkg/libvmaf` on it), Python (ctypes) | WP1, WP2 headers | per language |
| WP8 | Option groups -> CLI table / usage, MCP schemas (both servers), proto messages, OpenAPI components | WP1 | WP2-WP7 |
| WP9 | `vmafx`, `vmafx_tune`, `vmafx_pre` FFmpeg filters, `-vmafx-profile` (D6) | WP3, WP4, WP5, WP8 | |
| WP10 | Remove every `vmaf`-named filter / option patch, refresh the series, migration guide | WP9 | |
| WP11 | Docs: generated reference, guides, migration | continuous | |
| WP12 | Packaging: `libvmafx.pc`, SONAME, install layout, container and release images | WP6 | |

Critical path: WP1 -> WP2 -> WP3 -> WP9 -> WP10. WP4, WP5, WP7, WP8 run beside WP3.

### 6.2 Test strategy

- **Failing first**, per WP: each new function lands with a test that fails without it; each gate with a planted defect it refuses (drift check: a hand-edited header; append-only checker: a reordered field, a renumbered constant, a removed function; layout test: a field type change in a copy).
- **Golden-data gate through compat**: unchanged test sources; after WP6 the CLI and the Python harness reach the engine through compat. Run as today, plus once with the compat library forced (`LD_LIBRARY_PATH` to the split build) in the release workflow.
- **Bit-exact parity through the new API**: the cross-backend parity gate (`scripts/ci/cross_backend_parity_gate.py`) gains an import mode: each twin declared exact scores an imported device frame `==` the host-uploaded frame and `==` the CPU extractor; tolerance cells keep their bounds.
- **Fences**: tests that fail when the acquire wait or the release signal is skipped (contention harness of ADR-1199).
- **Windows**: synthetic sessions with imported scores (deterministic) check boundaries, the partial last window, subsampling and sync == async.
- **Compat conformance**: generated old-vs-new cases, `%a` equality.
- **Bindings**: each binding's tests call the library built by Meson (Python through `meson test`, Rust through `cargo test` with the build's library path, Go through `go test` with `CGO_LDFLAGS` as `pkg/libvmaf/doc.go` describes).
- **Upstream FFmpeg**: builds against the installed tree and scores three frames.

## 7. Decisions (maintainer, 2026-10-05)

All eight were answered by popup on 2026-10-05; ADR-1852 records them and is Accepted.

| # | Question | Answer | What follows |
| --- | --- | --- | --- |
| D1 | Generation source of truth | In-house TOML + Python generators (Recommended) | `core/api/vmafx.toml` + `scripts/codegen/vmafx_api/`; outputs committed and drift-checked |
| D2 | Library name and SONAME | `libvmafx.so.1` (Recommended) | pkg-config `libvmafx`, headers `vmafx/*.h`, ABI 1.0.0 frozen at the `v1.0.0` tag, number line independent of the product version |
| D3 | Compat packaging | Separate thin `libvmaf.so.3` (Recommended) | `libvmaf.so.3` links only exported `vmafx_` symbols of `libvmafx.so.1`; pkg-config `libvmaf` gains `Requires: libvmafx`; 2.0 drops it |
| D4 | Filter name and options | `vmafx`, `backend` + `device` (Recommended) | Section 5.2; upstream option names kept as aliases |
| D5 | Status codes | Own `VMAFX_*` codes + errno map (Recommended) | Generated errno map for compat; bindings map once from the same table |
| D6 | `libvmaf_tune` and the other filters | Custom: every filter capability survives under a VMAFx name, none under a `vmaf` name | Section 5.9: `vmafx`, `vmafx_tune`, `vmafx_pre`, `-vmafx-profile`; old names removed in the same step |
| D7 | Deprecation warnings on `libvmaf.h` | Opt-in 1.0, default 1.1, gone 2.0 (Recommended) | `VMAF_ENABLE_DEPRECATION_WARNINGS` in 1.0 |
| D8 | Import rule | One retry, then fail named (Recommended) | Section 5.4 as written |

## 8. Risks and open points

- **Scope**: RC4 already holds the Rust metric and the import API. The API, generator and filter add an estimated 12 work packages; WP3 needs macOS, SYCL and HIP devices for its exit evidence (ADR-1829 consequence).
- **Engine coupling**: the engine is `core/src/libvmaf.c` (4,923 lines) behind `struct VmafContext`. WP2 moves the bodies of the old functions behind `vmaf_engine_*` internal entry points (the prototype does this for four) rather than rewriting the engine.
- **Frame retention** makes pool sizing part of the contract; producers with small pools (three frames) and an extractor reading n-2 are refused today (ADR-1478); the new API reports the depth instead of failing late.
- **Platform fences**: Windows shared-texture fences and the dma-buf `sync_file` minimum kernel version need verification in WP3.
- **Windows packaging**: ADR-0121 builds Windows statically today; the split library (D3) applies to shared builds; static builds ship `libvmafx.a` and `libvmaf.a`.

## 9. Sources (checked 2026-10-05)

- cbindgen: <https://github.com/mozilla/cbindgen> (C/C++11 headers from Rust; MPL-2.0); version from <https://crates.io/api/v1/crates/cbindgen> (0.29.4, 2026-06-09).
- UniFFI: <https://github.com/mozilla/uniffi-rs> (Kotlin, Swift, Python, Ruby built in; third-party Go, C#, ...; MPL-2.0; "a long way from a 1.0 release"), <https://mozilla.github.io/uniffi-rs/latest/internals/design_principles.html>; crates.io 0.32.2 (2026-09-23).
- Diplomat: <https://github.com/rust-diplomat/diplomat>, <https://rust-diplomat.github.io/diplomat/>, <https://rust-diplomat.github.io/diplomat/types.html> ("Callbacks in parameters. Support is limited."), design doc <https://github.com/rust-diplomat/diplomat/blob/main/docs/design_doc.md> (Rust implementations only, opaque `Box` types); crates.io `diplomat-tool` 0.16.1 (2026-08-20, MIT OR Apache-2.0, rust-version 1.88).
- wit-bindgen: <https://github.com/bytecodealliance/wit-bindgen> (WebAssembly components only; "This CLI IS NOT stable"); crates.io 0.62.0 (2026-09-10).
- protobuf plugins: <https://protobuf.dev/reference/other/> (`CodeGeneratorRequest` / `CodeGeneratorResponse`, any language); custom options <https://protobuf.dev/programming-guides/proto3/#customoptions>; release v36.2 (2026-09-17) from the project's GitHub releases.
- buf breaking: <https://buf.build/docs/breaking/> (FILE / PACKAGE / WIRE_JSON / WIRE; `--against '.git#branch=main'`); buf v1.73.0 (2026-09-11), Apache-2.0.
- bindgen: crates.io 0.73.2 (2026-09-08, BSD-3-Clause).
- SWIG licence: <https://www.swig.org/legal.html>.
- libabigail: <https://sourceware.org/libabigail/> (Apache-2.0 with LLVM exception).
- MCP tool schemas: <https://modelcontextprotocol.io/specification/2025-11-25/server/tools> (`inputSchema` / `outputSchema` default to JSON Schema 2020-12).
- FFmpeg facts from the local `n9.0.1` checkout: `libavutil/hwcontext.h` (device types; no HIP type), `libavutil/hwcontext_cuda.h` (`AVCUDADeviceContext { cuda_ctx; stream; }`), `libavfilter/vf_libvmaf.c` (symbols and options), `configure` (`require_pkg_config libvmaf "libvmaf >= 2.0.0"`), `fftools/ffmpeg_opt.c` (loopback decoders, `-dec`).
- dma-buf `sync_file` ioctls: `/usr/include/linux/dma-buf.h` on the build host (`DMA_BUF_IOCTL_EXPORT_SYNC_FILE`, `DMA_BUF_IOCTL_IMPORT_SYNC_FILE`).
- Repository facts: paths cited inline, read at `origin/master` `8eabe7c56`.

## 10. Prototype

- Draft PR [#2173](https://github.com/VMAFx/vmafx/pull/2173), label `rc4`, branch `rc4/api-generation-prototype`, head `5a90351ab`, rebased on `9bc68a108`; ADR-1852 Accepted 2026-10-05 (docs PR `docs/adr-vmafx-api-redesign`); digest Research-2158 (this document, public form).
- Definition `core/api/vmafx.toml`; generator `scripts/codegen/vmafx-api.py` + `scripts/codegen/vmafx_api/` (standard-library Python, 1,861 lines, 179 of them tests); outputs: `core/include/vmafx/vmafx.h`, `core/include/vmafx/libvmaf_bridge.h`, `core/src/vmafx/status_gen.{c,h}`, `core/src/vmafx/compat_libvmaf_gen.c`, `core/test/test_vmafx_abi_layout.c`, `bindings/python/vmafx/_api.py`, `docs/api/vmafx/reference.md`.
- Hand-written core: `core/src/vmafx/context.c`, `error.c`, `engine.h`; `libvmaf.c` renames the bodies of `vmaf_init`, `vmaf_close`, `vmaf_version`, `vmaf_feature_score_at_index` to `vmaf_engine_*` and adds `VmafContext.api_owner`.
- Evidence (CPU build, `-j4`): fast suite 345 OK / 0 fail; golden-data gate through the shims 280 passed, 3 skipped; `test_vmafx_api_slice` 11/11, `test_vmafx_python_binding` 7/7, generator tests 15/15; clang-tidy cpu lane (container, 22.1.8) 0 findings on the 7 touched translation units; `praetorctl audit` no finding in touched files; MSVC-ism preflight pass; every new gate shown refusing a planted defect.
