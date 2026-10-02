/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  Probe of vif_cuda's logarithm for test_cuda_vif_log2_table (ADR-1456).
 *  The extractor never loads this module.
 *
 *  The CPU `vif` reads its logarithms from a table its host math library
 *  filled (vif_log2_table_generate()); the statistic of vif_cuda evaluates
 *  the same expression on the device, log_generate() in vif_statistics.cuh.
 *  This kernel writes log_generate() for every argument of the table's
 *  domain, so the test can compare all entries with the host table and the
 *  twin's exactness does not rest on the frames a fixture happens to reach.
 *
 *  It is its own module, built by the same rule and with the same flags as
 *  filter1d.cu, which holds the statistic: log_generate() is one inline
 *  function of one header, and its value does not depend on the kernel it is
 *  inlined into.
 */

#include "cuda_helper.cuh"
#include "common.h"

#include "vif_statistics.cuh"

extern "C" {

/* table[i] = log_generate(offset + i) for i < count. */
__global__ void vif_log2_table_probe(VmafCudaBuffer table, unsigned offset, unsigned count)
{
    const unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count)
        reinterpret_cast<uint16_t *>(table.data)[i] = log_generate((int)(offset + i));
}

} /* extern "C" */
