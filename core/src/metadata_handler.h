/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/**
 *
 *  Copyright 2016-2026 Netflix, Inc.
 *
 *     Licensed under the BSD+Patent License (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         https://opensource.org/licenses/BSDplusPatent
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */

#ifndef VMAF_SRC_METADATA_HANDLER_H_
#define VMAF_SRC_METADATA_HANDLER_H_

#include "metadata.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NOLINTBEGIN(modernize-use-using): C header included by C and C++ translation units; C has no `using`. ADR-1138. */
typedef struct VmafCallbackItem {
    VmafMetadataConfiguration metadata_cfg;
    void (*callback)(void *, VmafMetadata *);
    void *data;
    struct VmafCallbackItem *next;
} VmafCallbackItem;
/* NOLINTEND(modernize-use-using) */

/* NOLINTBEGIN(modernize-use-using): C header included by C and C++ translation units; C has no `using`. ADR-1138. */
typedef struct VmafCallbackList {
    VmafCallbackItem *head;
} VmafCallbackList;
/* NOLINTEND(modernize-use-using) */

int vmaf_metadata_init(VmafCallbackList **const metadata);

int vmaf_metadata_append(VmafCallbackList *metadata, const VmafMetadataConfiguration metadata_cfg);

int vmaf_metadata_destroy(VmafCallbackList *metadata);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VMAF_SRC_METADATA_HANDLER_H_ */
