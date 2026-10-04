#!/bin/sh
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
#
# add_spdx_header.sh — prepend the licence header the repository requires on
# every Go file (ADR-1250) to the files bpf2go generated. Run by gen.go's
# second go:generate directive; idempotent.
set -eu
for f in "$@"; do
  if head -n 1 "$f" | grep -q '^// Copyright'; then
    continue
  fi
  {
    printf '%s\n%s\n\n' '// Copyright 2026 Lusoris' '// SPDX-License-Identifier: EUPL-1.2'
    cat "$f"
  } >"$f.tmp"
  mv "$f.tmp" "$f"
done
