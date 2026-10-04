# Validating `vmaf_sycl_import_d3d11_surface` in a local Windows VM

Follow these steps when you change the Windows D3D11 surface-import path
and need to check it on real hardware. The path was added in
[ADR-0103](../adr/0103-sycl-d3d11-surface-import.md).

CI compiles and links the import path but never runs it. The required
`Windows MSVC+SYCL` lane builds the SYCL backend with MSVC and Intel
oneAPI, including `d3d11_import.cpp` against `d3d11` and `dxgi`. The
Windows runners have no GPU, so that lane runs no tests. Validation of
the import itself happens manually in a local Windows VM.

## When you need to run this

Run this procedure when you:

- changed
  [core/src/sycl/d3d11_import.cpp](../../core/src/sycl/d3d11_import.cpp);
- changed [core/src/sycl/common.cpp](../../core/src/sycl/common.cpp) in
  a way that affects `vmaf_sycl_upload_plane` (the import path's sink);
- bumped the Intel oneAPI DPC++ toolkit version.

Otherwise CI is enough: `Windows MSVC+SYCL` proves the import path still
compiles and links, and the Linux SYCL lanes compile the shared SYCL
code.

## Procedure

1. Prepare the VM.
2. Build libvmaf with the SYCL backend.
3. Run the smoke test.
4. Run the full reproducer with a real D3D11 surface.
5. Report the results in the PR description.

### 1. Prepare the VM

- Windows 10 or 11 (x64) with an Intel GPU. An integrated GPU (Xe, UHD
  Graphics) is fine; the D3D11 path does not need a discrete Arc.
- [Intel oneAPI Base
  Toolkit](https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit-download.html),
  which installs `icx` / `icpx` and the SYCL runtime.
- Git for Windows, or a network share that mounts the repository from
  the host.
- Meson and Ninja (`pip install meson ninja`).

### 2. Build

From an Intel oneAPI command prompt (`icpx` on `PATH`, environment
variables set). The Meson source directory is `core`:

```powershell
cd C:\path\to\vmaf
meson setup build-sycl core ^
    -Denable_cuda=false -Denable_sycl=true ^
    --buildtype release
ninja -C build-sycl
```

!!! note
    If the link step fails with an unknown `-fsycl` flag, the shell is
    dropping the Intel oneAPI wrappers. Re-open the shell through the
    oneAPI launcher and retry.

### 3. Smoke test (EINVAL paths)

The EINVAL paths exercise the argument validation without a real
decoder. Save this as `test_d3d11_smoke.c` on the VM:

```c
#include <stdio.h>
#include <libvmaf/libvmaf_sycl.h>

int main(void) {
    /* Every invalid-argument call must return < 0 without crashing. */
    int bad = vmaf_sycl_import_d3d11_surface(NULL, NULL, NULL, 0, 0, 0, 0, 0);
    printf("null pointers => %d\n", bad);
    return bad < 0 ? 0 : 1;
}
```

Build and run it:

```powershell
icx test_d3d11_smoke.c ^
    -I core\include -I build-sycl\include ^
    /link build-sycl\src\libvmaf.lib
.\test_d3d11_smoke.exe
```

Expected output: `null pointers => -22` (`-EINVAL`), exit code 0.

### 4. Full reproducer (real D3D11 surface)

The import path needs a real decoded surface. The fastest source is
the MediaFoundation H.264 decoder on a short clip: it outputs
`IMFSample`s backed by `ID3D11Texture2D`.

The pseudocode below uses the real signatures from
[`libvmaf_sycl.h`](../../core/include/libvmaf/libvmaf_sycl.h).
`vmaf_sycl_state_init()` takes the `VmafSyclConfiguration` by value, and
`vmaf_sycl_init_frame_buffers()` takes the `VmafContext` that the state
was imported into.

```c
/* pseudocode: only the vmaf_sycl_* calls are the real API */
ID3D11Device *dev; ID3D11DeviceContext *ctx;
D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                  NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx);

/* Decode one frame, extract its ID3D11Texture2D via
 * IMFDXGIBuffer::GetResource. Call it tex. */

VmafSyclState *sycl_state;
VmafSyclConfiguration cfg = { .device_index = 0, .enable_profiling = 0 };
vmaf_sycl_state_init(&sycl_state, cfg);
/* vmaf: a VmafContext created with vmaf_init(); import the state first
 * with vmaf_sycl_import_state(vmaf, sycl_state). */
vmaf_sycl_init_frame_buffers(vmaf, 1920, 1080, 8);

int rc = vmaf_sycl_import_d3d11_surface(sycl_state, dev, tex,
                                        /* subresource */ 0,
                                        /* is_ref */ 1,
                                        1920, 1080, 8);
printf("import rc = %d\n", rc);  /* expect 0 */
```

Record the timing per frame. An order-of-magnitude slower result than
the other frames in the run suggests the staging texture is not in
CPU-accessible memory or that the SYCL host-to-device copy is stalling
behind a previous submit. The older expectation for a 1080p 8-bit frame
on a typical integrated GPU over PCIe Gen3 x8 was about 2 ms end to end;
it is a rough target, not a measured gate.

### 5. Report the results

Paste this template into the PR description:

```text
- Windows build: <SHA>, oneAPI toolkit <version>
- Smoke test: pass / fail, errno values seen
- Full reproducer: frames processed, mean per-frame us, GPU model
```

That is enough evidence for review: no screen recording and no log
upload.
