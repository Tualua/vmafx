#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Only the controller's service account may read VmafxTenants (ADR-1592).

Every Role and ClusterRole the chart renders is resolved through its bindings
to the service accounts it grants; the accounts that reach ``vmafxtenants``
must be exactly the controller's own, which only the controller pods use.
Positive (tenant registry on), negative (no other workload's account, the
operator's included) and boundary (no registry: nobody) renders; the
helm-chart workflow runs this file and needs ``helm`` on PATH.
"""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Any

import yaml  # type: ignore[import-untyped]

CHART = Path(__file__).resolve().parents[3] / "deploy" / "helm" / "vmafx"
REGISTRY = """
controller: {enabled: true}
node: {enabled: true}
operator: {enabled: true}
networkPolicy: {enabled: true}
auth:
  enabled: true
  issuer: https://idp.example.com/
  jwksEndpoint: https://idp.example.com/keys
  tenants:
    - tenantId: acme
"""


def render(values: str) -> list[dict[str, Any]]:
    with tempfile.NamedTemporaryFile("w", suffix=".yaml") as fh:
        fh.write(values)
        fh.flush()
        cmd = ["helm", "template", "vmafx", str(CHART), "--namespace", "vmafx", "-f", fh.name]
        result = subprocess.run(  # noqa: S603 -- fixed helm executable; argv built here
            cmd, capture_output=True, text=True, check=False
        )
    if result.returncode != 0:
        raise AssertionError(f"helm template failed: {result.stderr}")
    return [doc for doc in yaml.safe_load_all(result.stdout) if doc]


def grants_tenants(role: dict[str, Any]) -> bool:
    return any("vmafxtenants" in rule.get("resources", []) for rule in role.get("rules", []))


def tenant_readers(docs: list[dict[str, Any]]) -> set[str]:
    """Service accounts any binding grants a rule on vmafxtenants."""
    roles = {
        (d["kind"], d["metadata"]["name"]): d for d in docs if d["kind"] in ("Role", "ClusterRole")
    }
    readers: set[str] = set()
    for binding in (d for d in docs if d["kind"] in ("RoleBinding", "ClusterRoleBinding")):
        ref = binding["roleRef"]
        role = roles.get((ref["kind"], ref["name"]))
        if role is None or not grants_tenants(role):
            continue
        readers |= {s["name"] for s in binding.get("subjects", []) if s["kind"] == "ServiceAccount"}
    return readers


def pod_accounts(docs: list[dict[str, Any]]) -> dict[str, str]:
    return {
        d["metadata"]["name"]: d["spec"]["template"]["spec"]["serviceAccountName"]
        for d in docs
        if d["kind"] in ("Deployment", "StatefulSet", "Job")
    }


class HelmServiceAccountTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if shutil.which("helm") is None:
            raise AssertionError("helm is not on PATH; this test renders the chart")

    def test_only_the_controller_reads_tenants(self) -> None:
        docs = render(REGISTRY)
        accounts = pod_accounts(docs)
        self.assertEqual(accounts["vmafx-controller"], "vmafx-controller")
        self.assertEqual(tenant_readers(docs), {"vmafx-controller"})
        others = {name: sa for name, sa in accounts.items() if name != "vmafx-controller"}
        self.assertTrue(others, "the render has no other workload to compare with")
        for name, sa in others.items():
            self.assertNotEqual(sa, "vmafx-controller", f"{name} runs as the controller's account")
        account_names = {d["metadata"]["name"] for d in docs if d["kind"] == "ServiceAccount"}
        self.assertIn("vmafx-controller", account_names)

    def test_operator_cluster_role_has_no_tenant_rule(self) -> None:
        crds = next(d for d in render(REGISTRY) if d["kind"] == "ClusterRole")
        self.assertFalse(grants_tenants(crds), "the operator has no tenant reconciler")

    def test_without_a_registry_nobody_reads_tenants(self) -> None:
        docs = render(
            "controller: {enabled: true}\nnode: {enabled: true}\noperator: {enabled: true}\n"
            "auth: {enabled: true, disabled: true}\n"
        )
        self.assertEqual(tenant_readers(docs), set())


if __name__ == "__main__":
    unittest.main()
