# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The vmaf CLI never returns a negative status from main().

libvmaf's error codes are negative errno values. POSIX truncates a status
returned from main() to eight bits (-22 becomes 234); Windows keeps 32 bits,
and the documented "libvmaf code modulo 256" (docs/usage/cli.md) is not what
the process reports. Every value `vmaf_cli_main()` returns on the run path has
to pass through `vmaf_cli_exit_status()`.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VMAF_CPP = ROOT / "core" / "tools" / "vmaf.cpp"


def _vmaf_cli_main_body() -> str:
    text = VMAF_CPP.read_text(encoding="utf-8")
    match = re.search(r"int vmaf_cli_main\(int argc, char \*argv\[\]\)\s*\{(.*?)\n\}\n", text, re.S)
    assert match is not None, "vmaf_cli_main() not found in core/tools/vmaf.cpp"
    return match.group(1)


class CliExitStatusContract(unittest.TestCase):
    def test_include_present(self) -> None:
        self.assertIn('#include "cli_exit_status.h"', VMAF_CPP.read_text(encoding="utf-8"))

    def test_run_result_goes_through_the_helper(self) -> None:
        body = _vmaf_cli_main_body()
        returns = re.findall(r"return\s+(.*?);", body, re.S)
        self.assertTrue(returns)
        run_returns = [r for r in returns if "run_err" in r]
        self.assertTrue(run_returns, "no return of run_err in vmaf_cli_main()")
        for expr in run_returns:
            self.assertTrue(
                expr.lstrip().startswith("vmaf_cli_exit_status("),
                f"run result returned without vmaf_cli_exit_status(): return {expr}",
            )

    def test_no_bare_negative_return(self) -> None:
        body = _vmaf_cli_main_body()
        self.assertIsNone(re.search(r"return\s+-", body))


if __name__ == "__main__":
    unittest.main()
