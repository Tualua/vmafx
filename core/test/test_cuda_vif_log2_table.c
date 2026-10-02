/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * vif_cuda's log2 table is the CPU's, entry by entry (ADR-1462).
 *
 * The fixed-point `vif` extractor is integer arithmetic except for one or two
 * logarithms per pixel, which the CPU reads from a table of
 * VIF_LOG2_TABLE_SIZE uint16 entries that its host math library fills
 * (vif_log2_table_generate()). vif_cuda's statistic reads the same table from
 * a global of its kernel module; the host uploads it at init with
 * vmaf_cuda_vif_upload_log2_table(). It evaluates no logarithm on the device,
 * whose log2f() need not return the host's bits (ADR-1435).
 *
 * This test loads the vif module as the extractor does, checks that the
 * table is empty before the upload, runs the extractor's upload, reads the
 * table back through the module's transfer kernel (so it sees what the
 * statistic kernels see) and compares all entries with the host table. It
 * also checks that the upload fails on a module that has no such table.
 *
 * Skip behaviour: exits 77 when there is no CUDA device.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "test.h"

#include "cuda/common.h"
#include "feature/cuda/integer_vif_cuda.h"
#include "feature/vif_log2_table.h"
#include "libvmaf/libvmaf_cuda.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy proposes `nullptr`, but the required MSVC C build does
 * not provide that keyword. Preserve the portable C spelling. ADR-1138. */

/* A CUDA module that is not the vif one. */
extern const unsigned char psnr_score_ptx[];

#define MAX_REPORTED 8u

static uint16_t host_table[VIF_LOG2_TABLE_SIZE];
static uint16_t device_table[VIF_LOG2_TABLE_SIZE];

/* The module's table, as its kernels see it, copied into device_table. */
static int read_device_table(VmafCudaState *cu_state, CUmodule module, VmafCudaBuffer *staging)
{
    int err = vmaf_cuda_vif_log2_table_transfer(cu_state, module, staging, false);
    if (!err) {
        err = vmaf_cuda_buffer_download_async(cu_state, staging, device_table, 0);
    }
    const int sync_err = vmaf_cuda_sync(cu_state);
    return err ? err : sync_err;
}

/* Entries of device_table that are not the host's, the first few reported. */
static unsigned table_mismatches(void)
{
    unsigned mismatches = 0u;
    for (unsigned i = 0; i < VIF_LOG2_TABLE_SIZE; i++) {
        if (host_table[i] == device_table[i]) {
            continue;
        }
        if (mismatches < MAX_REPORTED) {
            (void)fprintf(stderr, "\nentry %u (argument %u): host %u device %u", i,
                          VIF_LOG2_TABLE_OFFSET + i, (unsigned)host_table[i],
                          (unsigned)device_table[i]);
        }
        mismatches++;
    }
    if (mismatches != 0u) {
        (void)fprintf(stderr, "\n%u of %u entries differ\n", mismatches, VIF_LOG2_TABLE_SIZE);
    }
    return mismatches;
}

static unsigned nonzero_entries(void)
{
    unsigned nonzero = 0u;
    for (unsigned i = 0; i < VIF_LOG2_TABLE_SIZE; i++) {
        nonzero += device_table[i] != 0u ? 1u : 0u;
    }
    return nonzero;
}

/* The outcome of one load, upload and read-back of the vif module. */
typedef struct UploadResult {
    int load;            /* loading filter1d and the other module */
    int before;          /* reading the table before the upload */
    unsigned nonzero;    /* entries set before the upload */
    int upload;          /* vmaf_cuda_vif_upload_log2_table() */
    int after;           /* reading the table after it */
    unsigned mismatches; /* entries that are not the host's */
    int wrong_module;    /* the upload into a module without the table */
    int release;         /* the staging buffer and both modules */
} UploadResult;

static int load_modules(VmafCudaState *cu_state, CUmodule *vif, CUmodule *other)
{
    CudaFunctions *cu_f = cu_state->f;
    CHECK_CUDA_RETURN(cu_f, cuCtxPushCurrent(cu_state->ctx));
    CUresult res = cu_f->cuModuleLoadData(vif, filter1d_ptx);
    if (res == CUDA_SUCCESS) {
        res = cu_f->cuModuleLoadData(other, psnr_score_ptx);
    }
    CHECK_CUDA_RETURN(cu_f, cuCtxPopCurrent(NULL));
    return res == CUDA_SUCCESS ? 0 : -EIO;
}

static void exercise_upload(VmafCudaState *cu_state, UploadResult *r)
{
    CUmodule vif = NULL;
    CUmodule other = NULL;
    VmafCudaBuffer *staging = NULL;
    r->load = load_modules(cu_state, &vif, &other);
    if (!r->load) {
        r->load = vmaf_cuda_buffer_alloc(cu_state, &staging, sizeof(device_table));
    }
    if (!r->load) {
        r->before = read_device_table(cu_state, vif, staging);
        r->nonzero = nonzero_entries();
        r->upload = vmaf_cuda_vif_upload_log2_table(cu_state, vif);
        r->after = read_device_table(cu_state, vif, staging);
        r->mismatches = table_mismatches();
        r->wrong_module = vmaf_cuda_vif_upload_log2_table(cu_state, other);
    }
    const int free_err = vmaf_cuda_buffer_free_owned(cu_state, &staging);
    const int unload_vif = vmaf_cuda_module_unload(cu_state, &vif);
    const int unload_other = vmaf_cuda_module_unload(cu_state, &other);
    r->release = free_err ? free_err : (unload_vif ? unload_vif : unload_other);
}

/* What the upload has to have done, in the order it happened: the message of
 * the first check that does not hold, or NULL. */
static char *check_upload(const UploadResult *r)
{
    const struct {
        char *message;
        bool holds;
    } checks[] = {
        {"loading the modules or allocating the staging buffer failed", !r->load},
        {"reading the module's table before the upload failed", !r->before},
        {"the module's table is not empty before the upload", r->nonzero == 0u},
        {"uploading the log2 table failed", !r->upload},
        {"reading the uploaded table back failed", !r->after},
        {"the device's log2 table is not the CPU's", r->mismatches == 0u},
        {"the upload must refuse a module without the table", r->wrong_module != 0},
        {"releasing the staging buffer or the modules failed", !r->release},
    };
    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
        if (!checks[i].holds) {
            return checks[i].message;
        }
    }
    return NULL;
}

static char *test_uploaded_table_is_the_cpu_table(void)
{
    VmafCudaState *cu_state = NULL;
    const VmafCudaConfiguration cuda_cfg = {0};
    if (vmaf_cuda_state_init(&cu_state, cuda_cfg) != 0 || !cu_state) {
        (void)fprintf(stderr, "[skip: no CUDA device] ");
        mu_skipped = 1;
        return NULL;
    }
    vif_log2_table_generate(host_table);
    UploadResult r = {.load = -EIO, .before = -EIO, .upload = -EIO, .after = -EIO};
    exercise_upload(cu_state, &r);
    const int release_err = vmaf_cuda_state_free(cu_state);
    mu_assert_msg(check_upload(&r));
    mu_assert("releasing the CUDA state failed", !release_err);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_uploaded_table_is_the_cpu_table);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
