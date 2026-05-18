#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
# Copyright 2026 Lusoris and Claude (Anthropic)
#
# dev/scripts/dev-mcp-entrypoint.sh — container entrypoint
#
# Keeps the container alive so MCP clients can attach via
#   docker exec -i vmaf-dev-mcp /opt/vmaf-venv/bin/vmaf-mcp
# (stdio transport — the only one vmaf-mcp's main() implements).
#
# Background: an earlier iteration tried to spawn vmaf-mcp with
# `--transport uds --socket …`, but vmaf-mcp's main() has no argparse
# and ignores all argv flags; it always opens a stdio_server() and
# blocks on stdin. Spawned as a background daemon, its stdin
# immediately closes → the process exits silently → the 30 s socket
# wait timed out → entrypoint exited 1 → docker restarted the
# container in a tight loop. The `.vscode/mcp.json` template and the
# project rule (ADR-0496) already document the docker-exec-i pattern;
# this entrypoint now just keeps the container alive for that pattern
# to work.
#
# If the project later grows a real UDS transport in vmaf-mcp, switch
# this script back to launching it as a daemon and waiting for the
# socket — the previous version of this file is in git.

set -euo pipefail

# ADR-0509: unset VK_ICD_FILENAMES / VK_DRIVER_FILES on container start.
# Docker's ENV directive cannot truly unset an env var — setting it to ""
# in the Containerfile produces an *empty* value, which the Vulkan loader
# treats as "no ICDs configured" and bails with ERROR_INCOMPATIBLE_DRIVER.
# Unset here so the loader falls back to its default search path
# (/etc/vulkan/icd.d/ + /usr/share/vulkan/icd.d/), which picks up:
#   - NVIDIA's nvidia_icd.json (bind-mounted by nvidia-container-runtime
#     when NVIDIA_DRIVER_CAPABILITIES includes `graphics`),
#   - Mesa's intel_icd.json / radeon_icd.json / lvp_icd.json
#     (installed by mesa-vulkan-drivers in the build-deps stage).
# Operators that need to force a single ICD can still set the env var at
# `docker exec` time per-invocation (e.g. `docker exec -e VK_ICD_FILENAMES=…`).
unset VK_ICD_FILENAMES VK_DRIVER_FILES || true

# ADR-0498 follow-up #8 (BBB e2e v2): some container runtimes ship a
# minimal ``/`` filesystem without ``/tmp`` (especially when the image
# is started with a fresh tmpfs overlay). Both the MCP log and the
# bug-cluster repro scripts assume ``/tmp`` exists with 1777
# permissions. Materialise it idempotently here so the entrypoint
# never fails on "No such file or directory: /tmp/vmaf-mcp.log".
mkdir -p /tmp && chmod 1777 /tmp

LOG_FILE="${VMAF_MCP_LOG:-/tmp/vmaf-mcp.log}"
MODEL_PATH="${VMAF_MODEL_PATH:-/workspace/model}"

# Banner so `docker logs vmaf-dev-mcp` is self-explanatory.
{
  echo "[dev-mcp-entrypoint] vmaf-dev-mcp container ready."
  echo "[dev-mcp-entrypoint] Build info: $(vmaf --version 2>&1 || echo 'vmaf CLI not in PATH')"
  echo "[dev-mcp-entrypoint] Model path: ${MODEL_PATH}"
  echo "[dev-mcp-entrypoint] vmaf-mcp transport: stdio (use 'docker exec -i ${HOSTNAME:-vmaf-dev-mcp} /opt/vmaf-venv/bin/vmaf-mcp')"
  echo "[dev-mcp-entrypoint] To run vmaf-tune / vmaf-tools inside, e.g.:"
  echo "[dev-mcp-entrypoint]   docker exec ${HOSTNAME:-vmaf-dev-mcp} vmaf --help"
  echo "[dev-mcp-entrypoint]   docker exec ${HOSTNAME:-vmaf-dev-mcp} bash -c 'cd /workspace && PYTHONPATH=tools/vmaf-tune/src python -c \"from vmaftune.cli import main; main()\" --help'"
} | tee -a "${LOG_FILE}"

# Keep container foreground; Docker's log collector reads stdout.
# We block on `tail -F` so any tool writing to the log shows up in
# `docker logs`. `tail` is shipped with coreutils in the base image.
touch "${LOG_FILE}"
exec tail -F "${LOG_FILE}"
