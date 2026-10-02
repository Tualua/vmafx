/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * vif_cuda's logarithm against the CPU's table, entry by entry (ADR-1456).
 *
 * The fixed-point `vif` extractor is integer arithmetic except for one or two
 * logarithms per pixel. The CPU reads them from a table of
 * VIF_LOG2_TABLE_SIZE uint16 entries that its host math library fills
 * (vif_log2_table_generate(): round(log2f(32768 + i) * 2048)). vif_cuda
 * evaluates the same expression per pixel on the device (log_generate() in
 * integer_vif/vif_statistics.cuh). A device's log2f() need not return the
 * host's bits: on an AMD gfx1036 it moved 77 entries and vif_hip matched the
 * CPU on 49 of 440 scores (ADR-1435).
 *
 * Equal scores on a set of frames do not show that the two agree: a frame
 * reaches the entries its variances select. This test launches the probe
 * kernel (integer_vif/vif_log2_probe.cu), which writes log_generate() for
 * every argument of the table's domain, and compares all entries with the
 * host table. While it
 * passes, vif_cuda adds the integers the CPU adds, whatever the frame.
 *
 * It fails on a host whose math library fills the table differently from
 * this device (another libm, another CUDA release). The fix is then to read
 * the host table on the device, as vif_hip does, not a tolerance.
 *
 * Skip behaviour: exits 77 when there is no CUDA device.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "test.h"

#include "cuda/common.h"
#include "feature/vif_log2_table.h"
#include "libvmaf/libvmaf_cuda.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy proposes `nullptr`, but the required MSVC C build does
 * not provide that keyword. Preserve the portable C spelling. ADR-1138. */

extern const unsigned char vif_log2_probe_ptx[];

#define PROBE_BLOCK 256u
#define MAX_REPORTED 8u

/* The probe kernel over the whole table into `device_table`. */
static int probe_launch(VmafCudaState *cu_state, CUfunction probe, VmafCudaBuffer *d_table)
{
    CudaFunctions *cu_f = cu_state->f;
    unsigned offset = VIF_LOG2_TABLE_OFFSET;
    unsigned count = VIF_LOG2_TABLE_SIZE;
    void *params[] = {(void *)d_table, &offset, &count};
    const unsigned grid = (VIF_LOG2_TABLE_SIZE + PROBE_BLOCK - 1u) / PROBE_BLOCK;
    CHECK_CUDA_RETURN(cu_f, cuCtxPushCurrent(cu_state->ctx));
    const CUresult launch =
        cu_f->cuLaunchKernel(probe, grid, 1, 1, PROBE_BLOCK, 1, 1, 0, cu_state->str, params, NULL);
    CHECK_CUDA_RETURN(cu_f, cuCtxPopCurrent(NULL));
    return launch == CUDA_SUCCESS ? 0 : -EIO;
}

static int probe_load(VmafCudaState *cu_state, CUmodule *module, CUfunction *probe)
{
    CudaFunctions *cu_f = cu_state->f;
    CHECK_CUDA_RETURN(cu_f, cuCtxPushCurrent(cu_state->ctx));
    CUresult res = cu_f->cuModuleLoadData(module, vif_log2_probe_ptx);
    if (res == CUDA_SUCCESS) {
        res = cu_f->cuModuleGetFunction(probe, *module, "vif_log2_table_probe");
    }
    CHECK_CUDA_RETURN(cu_f, cuCtxPopCurrent(NULL));
    return res == CUDA_SUCCESS ? 0 : -EIO;
}

/* Every entry of the table as the device evaluates it. Returns the first
 * error; the buffer and the module are released on every path. */
static int device_table(VmafCudaState *cu_state, uint16_t *table)
{
    CUmodule module = NULL;
    CUfunction probe = NULL;
    VmafCudaBuffer *d_table = NULL;
    int err = probe_load(cu_state, &module, &probe);
    if (!err) {
        err = vmaf_cuda_buffer_alloc(cu_state, &d_table, VIF_LOG2_TABLE_SIZE * sizeof(*table));
    }
    if (!err) {
        err = probe_launch(cu_state, probe, d_table);
    }
    if (!err) {
        err = vmaf_cuda_buffer_download_async(cu_state, d_table, table, 0);
    }
    if (!err) {
        err = vmaf_cuda_sync(cu_state);
    }
    const int free_err = vmaf_cuda_buffer_free_owned(cu_state, &d_table);
    const int unload_err = vmaf_cuda_module_unload(cu_state, &module);
    if (err) {
        return err;
    }
    return free_err ? free_err : unload_err;
}

/* Entries of `device` that are not the host's, the first few reported. */
static unsigned table_mismatches(const uint16_t *host, const uint16_t *device)
{
    unsigned mismatches = 0u;
    for (unsigned i = 0; i < VIF_LOG2_TABLE_SIZE; i++) {
        if (host[i] == device[i]) {
            continue;
        }
        if (mismatches < MAX_REPORTED) {
            (void)fprintf(stderr, "\nentry %u (argument %u): host %u device %u", i,
                          VIF_LOG2_TABLE_OFFSET + i, (unsigned)host[i], (unsigned)device[i]);
        }
        mismatches++;
    }
    if (mismatches != 0u) {
        (void)fprintf(stderr, "\n%u of %u entries differ\n", mismatches, VIF_LOG2_TABLE_SIZE);
    }
    return mismatches;
}

static char *test_device_log2_equals_the_cpu_table(void)
{
    static uint16_t host[VIF_LOG2_TABLE_SIZE];
    static uint16_t device[VIF_LOG2_TABLE_SIZE];
    VmafCudaState *cu_state = NULL;
    const VmafCudaConfiguration cuda_cfg = {0};
    if (vmaf_cuda_state_init(&cu_state, cuda_cfg) != 0 || !cu_state) {
        (void)fprintf(stderr, "[skip: no CUDA device] ");
        mu_skipped = 1;
        return NULL;
    }
    /* A device that writes nothing must not pass on a zeroed buffer. */
    for (unsigned i = 0; i < VIF_LOG2_TABLE_SIZE; i++) {
        device[i] = UINT16_MAX;
    }
    vif_log2_table_generate(host);
    const int probe_err = device_table(cu_state, device);
    const int release_err = vmaf_cuda_state_free(cu_state);
    mu_assert("probing the device's log2 table failed", !probe_err);
    mu_assert("releasing the CUDA state failed", !release_err);
    mu_assert("vif_cuda's device logarithm differs from the CPU's log2 table: read the host "
              "table on the device (ADR-1435), do not add a tolerance",
              table_mismatches(host, device) == 0u);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_device_log2_equals_the_cpu_table);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
