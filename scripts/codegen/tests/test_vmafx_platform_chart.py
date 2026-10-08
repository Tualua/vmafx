# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The Helm chart part of the platform definition and its emitters (ADR-2350 D13).

Positive: a small definition with every entry form emits the exact values
file and schema; the real definition reproduces values.yaml and a schema whose
text is the uniform layout of its value. Negative: every planted definition
defect is refused, a `k8s:` type missing from the subset is named. Boundary:
plain-scalar rules against PyYAML, the layout width, quoted path keys, the
default note column. The migration test compares the schema with the one at the
merge base with origin/master while that one predates the Kubernetes types.
"""

from __future__ import annotations

import json
import unittest
from typing import Any

import tomllib
import yaml  # type: ignore[import-untyped]
from support import ROOT
from vmafx_api import emit_chart, gitref
from vmafx_api.chart import NO_VALUE, parse_chart, split_path
from vmafx_api.model import DefinitionError
from vmafx_api.platform import DEFINITION, KUBERNETES_SUBSET

# The keys whose schema is a Kubernetes type, with the schema they had before
# (docs/development/k8s-deployment.md lists them under the upgrade notes).
UNTYPED = {"type": "array"}
OBJECT = {"type": "object"}
KUBERNETES_TYPED = {
    "tolerations": UNTYPED,
    "controller.tolerations": UNTYPED,
    "node.tolerations": UNTYPED,
    "nodeSelector": OBJECT,
    "controller.nodeSelector": OBJECT,
    "node.nodeSelector": OBJECT,
    "affinity": OBJECT,
    "topologySpreadConstraints": UNTYPED,
    "controller.topologySpreadConstraints": UNTYPED,
    "podSecurityContext": OBJECT,
    "securityContext": OBJECT,
    "livenessProbe": OBJECT,
    "readinessProbe": OBJECT,
    "node.volumes": UNTYPED,
    "node.volumeMounts": UNTYPED,
    "deployment.strategy": OBJECT,
    "node.strategy": {
        "type": "object",
        "description": "Rolling-update strategy for the node Deployment. Default: "
        "maxUnavailable:0, maxSurge:1 (zero-disruption, GPU-safe).",
    },
    "statefulSet.updateStrategy": OBJECT,
    "envFrom": UNTYPED,
    "podAnnotations": OBJECT,
    "serviceAccount.annotations": OBJECT,
    "ingress.annotations": OBJECT,
    "ingress.tls": UNTYPED,
    "monitoring.serviceMonitor.labels": OBJECT,
}

# Label and annotation maps and node selectors: string values, as the
# Kubernetes fields they become (ObjectMeta.annotations, PodSpec.nodeSelector).
STRING_MAP = {"type": "object", "additionalProperties": {"type": "string"}}
STRING_MAPS = {
    "nodeSelector",
    "controller.nodeSelector",
    "node.nodeSelector",
    "podAnnotations",
    "serviceAccount.annotations",
    "ingress.annotations",
    "monitoring.serviceMonitor.labels",
}


def definition() -> dict[str, Any]:
    with (ROOT / DEFINITION).open("rb") as handle:
        data: dict[str, Any] = tomllib.load(handle)
    return data


def kubernetes() -> dict[str, Any]:
    schemas: dict[str, Any] = json.loads((ROOT / KUBERNETES_SUBSET).read_text())["schemas"]
    return schemas


MINI = """
[chart_root]
schema_order = ["b", "a"]
values_tail = '''
# end
'''

[chart_root.schema]
type = "object"
additionalProperties = false

[[chart_defs]]
name = "port"

[chart_defs.schema]
type = "integer"
minimum = 1

[[chart]]
path = "a"
lead = '''
# Section a
'''

[chart.schema]
type = "object"

[[chart]]
path = "a.name"
value = "500m"
quoted = true
note = "a quantity"
note_column = 20

[chart.schema]
type = "string"

[[chart]]
path = "a.empty"
value = ""
note = "default note column"

[chart.schema]
type = "string"

[[chart]]
path = "a.port"
value = 8080

[chart.schema]
"$ref" = "#/$defs/port"

[[chart]]
path = "a.only"

[chart.schema]
type = "boolean"

[[chart]]
path = "b"
value = [{ host = "x.local", paths = [{ path = "/", pathType = "Prefix" }] }]

[chart.schema]
type = "array"
items = { "$ref" = "k8s:demo.Host" }

[[chart]]
path = "c"

[[chart]]
path = 'c."k.io/name"'
value = "yes"

[[chart]]
path = "d"
value = [1, 2]

[[chart]]
path = "e"
literal = true
value = "line\\n\\nindented: x\\n"
"""
KUBE = {
    "demo.Host": {
        "type": "object",
        "properties": {"paths": {"items": {"$ref": "#/$defs/demo.Path"}}},
    },
    "demo.Path": {"type": "object", "required": ["path"]},
    "demo.Unused": {"type": "string"},
}
MINI_VALUES = """# Section a
a:
  name: "500m"      # a quantity
  empty: ""  # default note column
  port: 8080
b:
  - host: x.local
    paths:
      - path: /
        pathType: Prefix
c:
  k.io/name: "yes"
d:
  - 1
  - 2
e: |
  line

  indented: x
# end
"""


def mini() -> dict[str, Any]:
    data: dict[str, Any] = tomllib.loads(MINI)
    return data


class ChartEmitTest(unittest.TestCase):
    def test_mini_definition_values(self) -> None:
        chart = parse_chart(mini())
        assert chart is not None
        text = emit_chart.values_text(chart)
        self.assertEqual(text, MINI_VALUES)
        parsed = yaml.safe_load(text)
        self.assertEqual(parsed["a"]["name"], "500m")
        self.assertEqual(parsed["c"], {"k.io/name": "yes"})

    def test_mini_definition_schema(self) -> None:
        chart = parse_chart(mini())
        assert chart is not None
        value = emit_chart.schema_value(chart, KUBE)
        self.assertEqual(list(value["properties"]), ["b", "a"])
        self.assertEqual(
            list(value["properties"]["a"]["properties"]), ["name", "empty", "port", "only"]
        )
        self.assertEqual(value["properties"]["b"]["items"], {"$ref": "#/$defs/demo.Host"})
        self.assertEqual(list(value["$defs"]), ["port", "demo.Host", "demo.Path"])
        self.assertEqual(list(value), ["type", "additionalProperties", "properties", "$defs"])

    def test_missing_kubernetes_type_is_named(self) -> None:
        chart = parse_chart(mini())
        assert chart is not None
        with self.assertRaisesRegex(DefinitionError, "demo.Host.*k8s_openapi.py"):
            emit_chart.schema_value(chart, {})

    def test_real_definition_reproduces_the_chart_files(self) -> None:
        chart = parse_chart(definition())
        assert chart is not None
        values = (ROOT / emit_chart.VALUES).read_text(encoding="utf-8")
        self.assertEqual(emit_chart.values_text(chart), values)
        schema = (ROOT / emit_chart.SCHEMA).read_text(encoding="utf-8")
        self.assertEqual(emit_chart.schema_text(chart, kubernetes()), schema)
        self.assertEqual(emit_chart.layout(json.loads(schema)), schema)

    def test_kubernetes_typed_keys_are_the_documented_ones(self) -> None:
        chart = parse_chart(definition())
        assert chart is not None
        schemas = {e.name: e.schema for e in chart.entries if e.schema is not None}
        typed = {name for name, schema in schemas.items() if "k8s:" in json.dumps(schema)}
        self.assertEqual(typed, set(KUBERNETES_TYPED) - STRING_MAPS)
        for name in STRING_MAPS:
            self.assertEqual(schemas[name], STRING_MAP, name)
        self.assertEqual(
            dict(chart.defs)["resourceSpec"],
            {"$ref": "k8s:io.k8s.api.core.v1.ResourceRequirements"},
        )


# (definition, the refusal it must give) per planted defect
BASE = '[chart_root.schema]\ntype = "object"\n'
DEFECTS = {
    "parent missing": (
        BASE + '[[chart]]\npath = "a.b"\nvalue = 1\n',
        "its parent comes first",
    ),
    "path twice": (
        BASE + '[[chart]]\npath = "a"\nvalue = 1\n[[chart]]\npath = "a"\nvalue = 2\n',
        "appears twice",
    ),
    "value above values": (
        BASE + '[[chart]]\npath = "a"\nvalue = {}\n[[chart]]\npath = "a.b"\nvalue = 1\n',
        "no keys with values below it",
    ),
    "neither": (BASE + '[[chart]]\npath = "a"\n', "neither a value nor a schema"),
    "lead with a key": (
        BASE + "[[chart]]\npath = \"a\"\nvalue = 1\nlead = '''\nb: 2\n'''\n",
        "only comment and blank lines",
    ),
    "quoted number": (
        BASE + '[[chart]]\npath = "a"\nvalue = 1\nquoted = true\n',
        "string value",
    ),
    "literal without newline": (
        BASE + '[[chart]]\npath = "a"\nvalue = "x"\nliteral = true\n',
        "ends with a newline",
    ),
    "note on a mapping": (
        BASE + '[[chart]]\npath = "a"\nnote = "x"\n[chart.schema]\ntype = "object"\n'
        '[[chart]]\npath = "a.b"\nvalue = 1\n',
        "belongs to a key with a value",
    ),
    "column without note": (
        BASE + '[[chart]]\npath = "a"\nvalue = 1\nnote_column = 9\n',
        "without a `note`",
    ),
    "unknown key": (
        BASE + '[[chart]]\npath = "a"\nvalue = 1\ncolour = "x"\n',
        "unknown keys",
    ),
    "properties written": (
        BASE + '[[chart]]\npath = "a"\nvalue = 1\n[chart.schema]\nproperties = {}\n',
        "come from the entries",
    ),
    "bad ref": (
        BASE + '[[chart]]\npath = "a"\nvalue = 1\n[chart.schema]\n"$ref" = "#/$defs/nope"\n',
        "neither a chart_defs name",
    ),
    "schema_order": (
        '[chart_root]\nschema_order = ["a", "z"]\n'
        + BASE
        + '[[chart]]\npath = "a"\nvalue = 1\n[chart.schema]\ntype = "integer"\n',
        "names each top-level key once",
    ),
    "ref with properties": (
        BASE + '[[chart_defs]]\nname = "d"\nschema = { type = "object" }\n'
        '[[chart]]\npath = "a"\n[chart.schema]\n"$ref" = "#/$defs/d"\n'
        '[[chart]]\npath = "a.b"\nvalue = 1\n[chart.schema]\ntype = "integer"\n',
        "a \\$ref schema has no properties",
    ),
    "malformed path": (BASE + '[[chart]]\npath = "a..b"\nvalue = 1\n', "dotted key"),
    "no root": ('[[chart]]\npath = "a"\nvalue = 1\n', "chart_root"),
}


class ChartValidationTest(unittest.TestCase):
    def _refused(self, toml: str, message: str) -> None:
        with self.assertRaisesRegex(DefinitionError, message):
            parse_chart(tomllib.loads(toml))

    def test_planted_definition_defects_are_refused(self) -> None:
        for label, (toml, message) in DEFECTS.items():
            with self.subTest(label):
                self._refused(toml, message)

    def test_without_chart_tables_there_is_no_chart(self) -> None:
        self.assertIsNone(parse_chart({}))


class ChartScalarTest(unittest.TestCase):
    def test_plain_scalars_read_back_as_written(self) -> None:
        cases = [
            "ok", "IfNotPresent", "/data/x", "0.0.0.0/0", "v1.2.3", "ghcr.io/a/b:1", "5m",
            "", "true", "No", "null", "~", "1", "1.5", "0x10", "1e3", ".inf", "12:30",
            "2026-10-08", "a: b", "a #b", "-x", "*x", "x:", " lead", "it's", "@x",
        ]  # fmt: skip
        for text in cases:
            with self.subTest(text):
                written = emit_chart.scalar(text)
                self.assertEqual(yaml.safe_load(f"k: {written}")["k"], text)
                if emit_chart.plain_safe(text):
                    self.assertEqual(written, text)

    def test_quoted_forces_quotes(self) -> None:
        self.assertEqual(emit_chart.scalar("ok", quoted=True), '"ok"')

    def test_layout_width(self) -> None:
        fits = {"k": "x" * (emit_chart.WIDTH - len('{"k": ""}'))}
        self.assertEqual(emit_chart.layout(fits).count("\n"), 1)
        wide = {"k": "x" * (emit_chart.WIDTH - len('{"k": ""}') + 1)}
        self.assertEqual(emit_chart.layout(wide), '{\n  "k": "' + "x" * 92 + '"\n}\n')
        self.assertEqual(emit_chart.layout({"a": [], "b": {}}), '{"a": [], "b": {}}\n')

    def test_path_keys(self) -> None:
        self.assertEqual(split_path('a."b.c".d'), ("a", "b.c", "d"))
        for bad in ("", "a.", ".a", 'a."b', 'a.""'):
            with self.subTest(bad):
                self.assertIsNone(split_path(bad))

    def test_entry_without_value(self) -> None:
        chart = parse_chart(mini())
        assert chart is not None
        only = next(e for e in chart.entries if e.name == "a.only")
        self.assertIs(only.value, NO_VALUE)


class ChartMigrationTest(unittest.TestCase):
    """While origin/master's schema has no Kubernetes types, this one differs
    from it only at KUBERNETES_TYPED, where it had the untyped forms."""

    def test_schema_differs_from_the_base_only_at_the_typed_keys(self) -> None:
        try:
            base_ref = gitref.merge_base(ROOT, "origin/master")
            files = gitref.files_at(ROOT, base_ref, "deploy/helm/vmafx", ".json")
        except gitref.Unavailable as exc:
            self.skipTest(f"no merge base with origin/master: {exc}")
        base_text = files.get(emit_chart.SCHEMA)
        if base_text is None or "io.k8s.api" in base_text:
            self.skipTest("the merge base already has the Kubernetes types")
        base = json.loads(base_text)
        ours = json.loads((ROOT / emit_chart.SCHEMA).read_text(encoding="utf-8"))
        for path, old in KUBERNETES_TYPED.items():
            keys = path.split(".")
            node, mine = base, ours
            for key in keys[:-1]:
                node, mine = node["properties"][key], mine["properties"][key]
            self.assertEqual(node["properties"][keys[-1]], old, path)
            mine["properties"][keys[-1]] = old
        base["$defs"]["resourceSpec"] = ours["$defs"]["resourceSpec"] = None
        ours["$defs"] = {k: v for k, v in ours["$defs"].items() if not k.startswith("io.k8s.")}
        self.assertEqual(json.dumps(ours), json.dumps(base))


if __name__ == "__main__":
    unittest.main()
