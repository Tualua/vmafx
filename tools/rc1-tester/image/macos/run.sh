#!/bin/sh
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
#
# Runs the VMAFx tester report from this unpacked directory and prints one JSON
# report on stdout (summary on stderr). Reads this directory, writes nothing
# outside it and your temporary directory. Needs no network: when macOS's
# sandbox-exec is available the run is started with network access denied.
set -eu

here=$(cd "$(dirname "$0")" && pwd)

if [ "$(uname -s)" != "Darwin" ] || [ "$(uname -m)" != "arm64" ]; then
  echo "run.sh: this bundle is for macOS on Apple silicon (arm64)" >&2
  exit 64
fi

export VMAFX_IMAGE_ROOT="$here"
python="$here/runtime/bin/python3"
script="$here/tester/vmaf-tester-report"
profile='(version 1)(allow default)(deny network*)'

if [ -x /usr/bin/sandbox-exec ] && /usr/bin/sandbox-exec -p "$profile" /usr/bin/true 2>/dev/null; then
  echo "run.sh: network access is denied for this run (sandbox-exec)" >&2
  exec /usr/bin/sandbox-exec -p "$profile" "$python" -I -B "$script" "$@"
fi
echo "run.sh: sandbox-exec unavailable; the report script uses no network code" >&2
exec "$python" -I -B "$script" "$@"
