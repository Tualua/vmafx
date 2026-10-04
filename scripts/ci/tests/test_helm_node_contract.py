#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The vmafx-node contract of the Helm chart, checked on real `helm template`
renders: the controller address the node's controller client reads and the
NetworkPolicy egress it needs.

Requires the `helm` binary (the helm-chart workflow installs it); a missing
binary is a failure, not a skip.
"""

from __future__ import annotations

import shutil
import subprocess
import unittest
from pathlib import Path
from typing import Any

import yaml  # type: ignore[import-untyped]

CHART = Path(__file__).resolve().parents[3] / "deploy" / "helm" / "vmafx"


def render(*sets: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    """Run `helm template` with the given --set values."""
    helm = shutil.which("helm")
    if helm is None:
        raise AssertionError("helm binary not found on PATH; install helm to run this test")
    argv = [helm, "template", "vmafx", str(CHART), "--namespace", "vmafx"]
    for value in sets:
        argv += ["--set", value]
    return subprocess.run(  # noqa: S603 -- resolved helm binary, fixed argv, no shell
        argv, capture_output=True, text=True, check=check
    )


def manifests(*sets: str) -> list[dict[str, Any]]:
    """Rendered manifests, empty documents dropped."""
    return [doc for doc in yaml.safe_load_all(render(*sets).stdout) if doc]


def node_container(*sets: str) -> dict[str, Any]:
    """The vmafx-node container of the node Deployment."""
    for doc in manifests("node.enabled=true", *sets):
        if doc["kind"] == "Deployment" and doc["metadata"]["name"].endswith("-node"):
            container: dict[str, Any] = doc["spec"]["template"]["spec"]["containers"][0]
            return container
    raise AssertionError("no node Deployment rendered")


def node_env(*sets: str) -> dict[str, str]:
    return {item["name"]: item.get("value", "") for item in node_container(*sets)["env"]}


def policy(name_suffix: str, *sets: str) -> dict[str, Any] | None:
    for doc in manifests("node.enabled=true", "networkPolicy.enabled=true", *sets):
        if doc["kind"] == "NetworkPolicy" and doc["metadata"]["name"].endswith(name_suffix):
            return doc
    return None


class ControllerAddress(unittest.TestCase):
    def test_unset_without_value(self) -> None:
        """No node.controllerAddr: the variable is absent, the node runs standalone."""
        self.assertNotIn("VMAFX_CONTROLLER_ADDR", node_env())

    def test_rendered_verbatim(self) -> None:
        env = node_env("node.controllerAddr=vmafx-controller.vmafx:9090")
        self.assertEqual(env["VMAFX_CONTROLLER_ADDR"], "vmafx-controller.vmafx:9090")


class NodeToControllerEgress(unittest.TestCase):
    def test_egress_to_controller_port(self) -> None:
        doc = policy("-allow-node-to-controller", "node.controllerAddr=ctrl:9090")
        assert doc is not None, "no node-to-controller egress policy rendered"
        self.assertEqual(doc["spec"]["policyTypes"], ["Egress"])
        ports = doc["spec"]["egress"][0]["ports"]
        self.assertEqual(ports, [{"protocol": "TCP", "port": 9090}])

    def test_port_follows_value(self) -> None:
        doc = policy(
            "-allow-node-to-controller",
            "node.controllerAddr=ctrl:7000",
            "networkPolicy.allow.nodeToController.port=7000",
        )
        assert doc is not None
        self.assertEqual(doc["spec"]["egress"][0]["ports"][0]["port"], 7000)

    def test_absent_without_controller(self) -> None:
        """Boundary: no controller address, no egress hole."""
        self.assertIsNone(policy("-allow-node-to-controller"))

    def test_schema_refuses_unknown_key(self) -> None:
        """Negative: a misspelt allow-rule key is refused by the schema."""
        result = render("networkPolicy.allow.nodeToController.prot=1", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("prot", result.stderr)


if __name__ == "__main__":
    unittest.main()
