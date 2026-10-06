# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
# shellcheck shell=bash
# Sourced helper for scripts/test/*.sh that run QSV zero-copy legs.
#
# Defines QSV_INIT: one VA-API device on the render node and one QSV device per
# decoder input (the decoders must not share a QSV session: docs/backends/sycl/overview.md).
# shellcheck disable=SC2034  # consumed by the sourcing script
QSV_INIT=(-init_hw_device vaapi=va0:/dev/dri/renderD128
  -init_hw_device qsv=qr@va0 -init_hw_device qsv=qd@va0)
