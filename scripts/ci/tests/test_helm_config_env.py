#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The chart's VMAFX_* environment comes from _config.gen.tpl (ADR-2350 D13).

Every VMAFX_* entry of every workload is rendered by a `vmafx.env.<workload>`
helper of templates/_config.gen.tpl, which scripts/codegen/vmafx-api.py writes
from the [[chart_env]] entries of api/vmafx-platform.toml. Positive: no other
template names a VMAFX_* entry. Planted change: an edited definition entry
changes the render, and only that entry. Migration (self-retiring): while the
chart at the merge base with origin/master still writes the lists by hand, the
generated helpers render every values set below byte for byte as it does;
once the merge base has _config.gen.tpl, or in a shallow clone without
origin/master (the helm-chart job's), the comparison is skipped. Boundary: the
values sets switch every condition of the entries on and off. The helm-chart
workflow runs this file and needs ``helm`` on PATH.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import tomllib

ROOT = Path(__file__).resolve().parents[3]
CHART = ROOT / "deploy" / "helm" / "vmafx"
TEMPLATES = CHART / "templates"
GENERATED = TEMPLATES / "_config.gen.tpl"
DEFINITION = ROOT / "api" / "vmafx-platform.toml"
HELM = shutil.which("helm") or "helm"
TIMEOUT_S = 120
ENTRY = re.compile(r"^\s*- name: VMAFX_", re.MULTILINE)

sys.path.insert(0, str(ROOT / "scripts" / "codegen"))
from vmafx_api import emit_config, gitref  # noqa: E402 -- path set above
from vmafx_api.config import parse_config  # noqa: E402

API = ["--api-versions", "postgresql.cnpg.io/v1"]
FULL = ["--set", "operator.enabled=true", "--set", "node.enabled=true", "--set", "controller.enabled=true",
        "--set", "auth.enabled=true", "--set", "auth.disabled=true", "--set", "podDisruptionBudget.enabled=true"]  # fmt: skip
SETS: dict[str, tuple[list[str], str]] = {
    "defaults": ([], ""),
    **{f"ci-{w}": (["--set", f"workload={w}", *FULL], "") for w in ("Deployment", "Job", "StatefulSet")},
    **{f"vendor-{v}": (["--set", f"gpu.vendor={v}", "--set", "node.enabled=true"], "") for v in ("nvidia", "amd", "intel", "cpu")},
    "kuttl-01": (["--set", "operator.enabled=true", "--set", "gpu.vendor=cpu"], ""),
    "provider-auth": ([], """
controller: {enabled: true}
node: {enabled: true, controllerToken: {secretName: node-token}}
operator: {enabled: true, leaderElect: true, logLevel: debug, controllerToken: {secretName: op-token}}
auth: {enabled: true, issuer: "https://idp.example.com/", jwksEndpoint: "https://idp.example.com/keys",
       audience: vmafx, tenantClaim: org, rolesClaim: roles, scoringRoots: ["/data/{tenant}"]}
"""),
    "tenant-registry": ([], """
controller: {enabled: true}
auth: {enabled: true, tenants: [{tenantId: acme, oidc: {issuer: "https://i.example.com/", jwksEndpoint: "https://i.example.com/k"}}]}
"""),
    "postgres-external": ([], """
controller: {enabled: true, replicas: 2, store: {backend: postgres, leaseTTL: 45s, sessionTTL: 2m, sweepInterval: 10s,
  backoffBase: 1s, backoffMax: 1m, postgresql: {mode: external, external: {secretName: pg-dsn, secretKey: dsn}}}}
auth: {enabled: true, disabled: true}
"""),
    "postgres-cnpg": ([], """
controller: {enabled: true, store: {backend: postgres, postgresql: {mode: cnpg}}}
auth: {enabled: true, disabled: true}
"""),
    "node-storage": ([], """
node: {enabled: true, grpcPort: 50060, metricsPort: 9100, ebpf: {enabled: true, mountPrefix: /rclone-mount/},
       fuse: {enabled: true, resourceName: devic.es/fuse}}
storage: {mode: mount, mountRoot: /rclone-mount/jobs, rclone: {config: "[r]\\ntype = s3\\n"}}
persistence: {models: {enabled: true, mountPath: /models}}
"""),
}  # fmt: skip


def render(chart: Path, args: list[str], values: str) -> str:
    with tempfile.NamedTemporaryFile("w", suffix=".yaml") as fh:
        fh.write(values)
        fh.flush()
        done = subprocess.run(  # noqa: S603 -- fixed helm executable; argv built here
            [HELM, "template", "vmafx", str(chart), *API, "-f", fh.name, *args],
            capture_output=True,
            text=True,
            check=False,
            timeout=TIMEOUT_S,
        )
    return done.stdout + done.stderr


class GeneratedEnvTest(unittest.TestCase):
    def test_no_other_template_writes_a_vmafx_entry(self) -> None:
        for path in sorted(TEMPLATES.rglob("*")):
            if path.is_file() and path != GENERATED:
                with self.subTest(path.name):
                    self.assertIsNone(ENTRY.search(path.read_text(encoding="utf-8")))
        self.assertGreater(len(ENTRY.findall(GENERATED.read_text(encoding="utf-8"))), 40)


@unittest.skipIf(shutil.which("helm") is None, "helm is not on PATH")
class RenderTest(unittest.TestCase):
    def test_a_definition_edit_changes_the_render(self) -> None:
        data = tomllib.loads(DEFINITION.read_text(encoding="utf-8"))
        entry = next(
            e
            for e in data["chart_env"]
            if e["env"] == "VMAFX_GRPC_LISTEN" and "node" in e["workloads"]
        )
        entry["value"] = '":{{ .Values.node.grpcPort | default 50052 }}0"'
        config = parse_config(data)
        assert config is not None
        args, values = SETS["provider-auth"]
        with tempfile.TemporaryDirectory() as tmp:
            chart = Path(tmp) / "vmafx"
            shutil.copytree(CHART, chart)
            (chart / "templates" / GENERATED.name).write_text(emit_config.template_text(config))
            edited = render(chart, args, values).splitlines()
        before = render(CHART, args, values).splitlines()
        changed = [(a, b) for a, b in zip(before, edited, strict=True) if a != b]
        self.assertEqual(
            changed, [('              value: ":50052"', '              value: ":500520"')]
        )

    def test_the_render_matches_the_hand_written_lists(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            try:
                ref = gitref.merge_base(ROOT, "origin/master")
                base = gitref.tree_at(ROOT, ref, "deploy/helm/vmafx", Path(tmp))
            except gitref.Unavailable as exc:
                self.skipTest(f"no chart at the merge base with origin/master: {exc}")
            if (base / "templates" / GENERATED.name).exists():
                self.skipTest("the merge base already generates the environment")
            for name, (args, values) in SETS.items():
                with self.subTest(name):
                    self.assertEqual(render(CHART, args, values), render(base, args, values))


if __name__ == "__main__":
    unittest.main()
